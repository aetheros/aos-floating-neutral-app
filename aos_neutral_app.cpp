// vim: sw=4 expandtab
// Copyright (c) Aetheros, Inc.  See COPYRIGHT

#include "FloatingNeutralConfig.hpp"

#include <aos/Log.hpp>
#include <aos/AppMain.hpp>
#include <m2m/AppEntity.hpp>
#include <xsd/m2m/Subscription.hpp>
#include <xsd/m2m/ContentInstance.hpp>
#include <xsd/m2m/Names.hpp>
#include <xsd/mtrsvc/MeterServicePolicy.hpp>
#include <xsd/mtrsvc/MeterRead.hpp>
#include <xsd/mtrsvc/Names.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <iostream>
#include <fstream>

using namespace std::chrono_literals;
using std::chrono::seconds;
using std::chrono::minutes;
using time_point = std::chrono::time_point< std::chrono::system_clock, std::chrono::seconds >;

class FloatingNeutral : public m2m::AppEntity
{
	std::mutex mutex_;

	FloatingNeutralConfig config_;

	bool thresholdsCurrentlyExceeded_{ false };
	bool haveLast_{ false };
	time_point lastTimestamp_;
	float lastVoltage_{ 0.0f };
	float lastCurrent_{ 0.0f };
	int debugTest_{ 0 };

	bool prepare()
	{
		activate();

		if( not initConfig( std::bind( &FloatingNeutral::processConfig, this, std::placeholders::_1 ) ) )
		{
			logError( "could not subscribe to config container" );
			return false;
		}

		processConfig( retrieveConfig() );

		deleteResource( "./metersvc/reads/" + getAppName() );
		if( not ensureSimpleContainer( "./metersvc/reads", getAppName(), {}, 3 ) )
		{
			logError( "could not ensure container ./metersvc/reads/" + getAppName() );
			return false;
		}

		if( not createPowerQualitySubscription() )
		{
			logError( "subscription creation failed" );
			return false;
		}

		if( not createMeterReadPowerQualityPolicy() )
		{
			logError( "power-quality meter read policy creation failed" );
			return false;
		}

		logInfo( "prepared" );
		return true;
	}

	void activate()
	{
		logInfo( "activating" );

		seconds backoffSeconds{ 30 };

		while( not AppEntity::activate() )
		{
			auto failureReason = getActivationFailureReason();
			logError( "activation failed: reason: " << failureReason );

			if( failureReason == m2m::ActivationFailureReason::Timeout or
				failureReason == m2m::ActivationFailureReason::NotRegistered )
			{
				continue;
			}
			
			logInfo( "retrying in " << backoffSeconds.count() << " seconds" );
			std::this_thread::sleep_for( backoffSeconds );

			backoffSeconds *= 2;
			if( backoffSeconds > minutes{16} )
			{
				backoffSeconds = seconds( minutes( 16 ) );
			}
		}

		logInfo( "activated" );
	}

	bool createPowerQualitySubscription()
	{
		deleteResource( "./metersvc/reads/" + getAppName() + "/power-quality-sub" );

		xsd::m2m::EventNotificationCriteria eventNotificationCriteria;
		eventNotificationCriteria.notificationEventType.assign()
			.push_back( xsd::m2m::NotificationEventType::Create_of_Direct_Child_Resource );

		auto rsp = createSimpleSubscription( "./metersvc/reads/" + getAppName(), "power-quality-sub",
			eventNotificationCriteria );
		auto const& status = rsp->responseStatusCode;

		logInfo( "power quality subscription: " << toString( status ) );

		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict =  status == xsd::m2m::ResponseStatusCode::CONFLICT;
		return created or conflict;
	}

	bool createMeterReadPowerQualityPolicy()
	{
		// policy attributes can change, so delete whatever is there first
		deleteResource( "./metersvc/policies/" + getAppName() + "-pol" );

		xsd::mtrsvc::ScheduleInterval scheduleInterval;
		scheduleInterval.end = nullptr;
		scheduleInterval.start = "2020-06-19T00:00:00";

		xsd::mtrsvc::TimeSchedule timeSchedule;
		timeSchedule.recurrencePeriod = config_.samplingPeriod;
		timeSchedule.scheduleInterval = std::move( scheduleInterval );

		xsd::mtrsvc::MeterReadSchedule meterReadSchedule;
		meterReadSchedule.readingType = "powerQuality";
		meterReadSchedule.timeSchedule = std::move( timeSchedule );
		meterReadSchedule.destContainer = getAppName();

		xsd::mtrsvc::MeterServicePolicy meterServicePolicy;
		meterServicePolicy = std::move( meterReadSchedule );

		auto response = createSimpleContentInstance( "./metersvc/policies", getAppName() + "-pol",
			xsd::toAnyTypeUnnamed( meterServicePolicy ) );
		auto const& status = response->responseStatusCode;

		logInfo( "power quality policy: " << toString( status ) );

		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict = status == xsd::m2m::ResponseStatusCode::CONFLICT;
		return created or conflict;
	}

	bool createServiceOffPolicy()
	{
		xsd::mtrsvc::MeterControlSchedule meterControlSchedule;
		meterControlSchedule.controlSchedule = "2020-06-19T00:00:00";
		meterControlSchedule.controlType = "serviceOff";

		xsd::mtrsvc::MeterServicePolicy meterServicePolicy;
		meterServicePolicy = std::move( meterControlSchedule );

		auto response = createSimpleContentInstance( "./metersvc/policies", getAppName() + "-control-pol",
			xsd::toAnyTypeUnnamed( meterServicePolicy ) );
		auto const& status = response->responseStatusCode;

		logInfo( "serviceOff policy creation: " << toString( status ) );

		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict = status == xsd::m2m::ResponseStatusCode::CONFLICT;
		return created or conflict;
	}

	void notificationCallback( m2m::Notification notification )
	{
		std::lock_guard<std::mutex> lock( mutex_ );

		auto const& event = notification.notificationEvent;
		if( not event.isSet() )
		{
			logWarn( "notification has no notificationEvent" );
			return;
		}

		auto const& eventType = event->notificationEventType;
		if( eventType != xsd::m2m::NotificationEventType::Create_of_Direct_Child_Resource )
		{
			logWarn( "got notification type " << toString( eventType) );
			return;
		}

		std::string const& subref = *notification.subscriptionReference;
		auto meterSubName = std::string( "power-quality-sub" );

		if( subref.find( meterSubName ) != std::string::npos )
		{
			logInfo( "got metersvc notification" );
			auto contentInstance = event->representation->extractNamed< xsd::m2m::ContentInstance >();
			auto meterRead = contentInstance.content->extractUnnamed< xsd::mtrsvc::MeterRead >();
			auto const& meterSvcData = *(meterRead.meterSvcData);
			processMeterSvcData( meterSvcData );
		}
		else
		{
			logWarn( "got unexpected notification with subscription reference: " << subref );
		}
	}

	void processConfig( std::string const& jsonConfig )
	{
		logInfo( "processing new config: " << jsonConfig );
		FloatingNeutralConfig newConfig( jsonConfig );
		sanitize( newConfig );
		config_ = std::move( newConfig );
		logInfo( "new config values: " << config_.dump() );
		// sampling period might have changed, so create new policy
		createMeterReadPowerQualityPolicy();
		haveLast_ = false;
	}

	void sanitize( FloatingNeutralConfig& newConfig )
	{
		std::vector<uint32_t> validSamplingPeriods
		{
			1, 2, 3, 4, 5, 6, 10, 12, 15, 20, 30, // valid seconds
			60*1, 60*2, 60*3, 60*4, 60*5, 60*6, 60*10, 60*12, 60*15, 60*20, 60*30, // valid minutes
			3600*1, 3600*2, 3600*3, 3600*4, 3600*6, 3600*8, 3600*12, 3600*24, // valid hours
		};
		uint32_t requestedPeriod = newConfig.samplingPeriod;
		uint32_t chosenPeriod{ 1 };
		for( auto validPeriod : validSamplingPeriods )
		{
			if( validPeriod > chosenPeriod and validPeriod <= requestedPeriod )
			{
				chosenPeriod = validPeriod;
			}
		}
		if( chosenPeriod != requestedPeriod )
		{
			logInfo( "requested sampling period " << requestedPeriod << " changing to " << chosenPeriod );
			newConfig.samplingPeriod = chosenPeriod;
		}
	}

	void processMeterSvcData( xsd::mtrsvc::MeterSvcData const& meterSvcData )
	{
		auto const& timestamp = meterSvcData.readTimeLocal;
		auto const& powerQuality = meterSvcData.powerQuality;

		if( not powerQuality.isSet() )
		{
			return;
		}

		//logInfo( "timestamp: " << *timestamp );
		//logInfo( "powerQuality: " << *powerQuality );

		// example timestamp format: 2023-07-14T10:15:00
		std::tm tm{};
		char* result = strptime( timestamp->c_str(), "%Y-%m-%dT%H:%M:%S", &tm );
		tm.tm_isdst = 0;
		if( result == NULL )
		{
			logWarn( "strptime was null" );
			return;
		}
		std::time_t time = std::mktime( &tm );
		auto thisTimestamp0 = std::chrono::system_clock::from_time_t( time );
		auto thisTimestamp = std::chrono::time_point_cast<seconds>( thisTimestamp0 );
		//logInfo( "count: " << thisTimestamp.time_since_epoch().count() );

		if( not powerQuality->voltageA.isSet() or not powerQuality->currentA.isSet() )
		{
			return;
		}

		float voltage = *powerQuality->voltageA;
		float current = *powerQuality->currentA;

		bool thresholdsExceeded = false;
		float deltaT = 0;
		float deltaV = 0;
		float deltaI = 0;

		if( haveLast_ )
		{
			deltaT = ( thisTimestamp - lastTimestamp_ ) / 1s;
			if( deltaT < 0.9 * config_.samplingPeriod )
			{
				logInfo( "power quality update too early, ignoring" );
				return;
			}
			if( deltaT > 1.1 * config_.samplingPeriod )
			{
				logInfo( "power quality update too late, skipping check" );
			}
			else
			{
				deltaV = std::abs( voltage - lastVoltage_ );
				deltaI = std::abs( current - lastCurrent_ );
				float variance = deltaV / deltaI;

				if( deltaI > config_.currentThreshold and variance > config_.varianceThreshold )
				{
					thresholdsExceeded = true;
				}

#if 0
				++debugTest_;
				if( debugTest_ > 2 )
				{
					thresholdsExceeded = true;
					debugTest_ = 0;
					logInfo( "forcing threshold exceeded to test alarm publishing" );
				}
#endif
			}
		}

		lastTimestamp_ = thisTimestamp;
		lastVoltage_ = voltage;
		lastCurrent_ = current;
		haveLast_ = true;

		if( thresholdsExceeded and not thresholdsCurrentlyExceeded_ )
		{
			logWarn( "loss-of-neutral thresholds exceeded: deltaI=" << deltaI << ", deltaV=" << deltaV );

			if( config_.disconnectService )
			{
				createServiceOffPolicy();
			}

			std::ostringstream arg;
			arg << "Loss Of Neutral: deltaV=" << deltaV << ", deltaI=" << deltaI;
			publishAlarm( arg.str() );
		}
		else if( not thresholdsExceeded and thresholdsCurrentlyExceeded_ )
		{
			logInfo( "loss-of-neutral: DeltaV/DeltaI thresholds returned to normal" );
			retractAlarm();
		}

		thresholdsCurrentlyExceeded_ = thresholdsExceeded;
	}

	void retractAlarm()
	{
		auto rsp = deleteResource( config_.alarmContainer + "/loss-of-neutral-alarm" );
		auto status = rsp->responseStatusCode;
		auto deleted = status == xsd::m2m::ResponseStatusCode::DELETED;
		auto not_found = status == xsd::m2m::ResponseStatusCode::NOT_FOUND;

		if( not deleted and not not_found )
		{
			logError( "could not retract alarm: " << *rsp );
		}
		else
		{
			logInfo( "retracted loss of neutral alarm" );
		}
	}

	void publishAlarm( std::string const& text )
	{
		deleteResource( config_.alarmContainer + "/loss-of-neutral-alarm" );

		auto rsp = createSimpleContentInstance( config_.alarmContainer, "loss-of-neutral-alarm", xsd::toAnyTypeUnnamed( text ) );
		auto status = rsp->responseStatusCode;
		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;

		if( not created )
		{
			logError( "could not publish alarm: " << *rsp );
		}
		else
		{
			logInfo( "published loss of neutral alarm" );
		}
	}

public:
    FloatingNeutral()
		: AppEntity( std::bind( &FloatingNeutral::notificationCallback, this, std::placeholders::_1 ) )
    {
    }

	void run()
	{
		{
			std::lock_guard<std::mutex> lock( mutex_ );

			while( not prepare() )
			{
				std::this_thread::sleep_for( seconds( 30 ) );
			}
		}

		waitForever();
	}
};

static void parseArgs( int argc, char* argv[] )
{
	bool exitUsage = false;
	int opt;
	while( ( opt = getopt( argc, argv, "d" ) ) != -1 )
	{
		switch( opt )
		{
		case 'd':
			aos::setLogLevel( aos::LogLevel::LOG_DEBUG );
		default:
			exitUsage = true;
		}
	}
	if( argc - optind > 1 )
	{
		exitUsage = true;
	}
	if( exitUsage )
	{
		std::cerr << "Usage " << argv[0] << " [-d]" << "\n";
		exit( EXIT_FAILURE );
	}
}

int main( int argc, char* argv[] )
{
    aos::AppMain appMain;
	parseArgs( argc, argv );

	logInfo( "starting" );

	FloatingNeutral app;
	app.run();

	logInfo( "exiting" );

	return 0;
}
