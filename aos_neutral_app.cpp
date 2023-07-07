// vim: sw=4 expandtab
// Copyright (c) Aetheros, Inc.  See COPYRIGHT
#include <aos/Log.hpp>
#include <aos/AppMain.hpp>
#include <m2m/AppEntity.hpp>
#include <xsd/m2m/Subscription.hpp>
#include <xsd/m2m/ContentInstance.hpp>
#include <xsd/m2m/Names.hpp>
#include <xsd/mtrsvc/MeterServicePolicy.hpp>
#include <xsd/mtrsvc/MeterRead.hpp>
#include <xsd/mtrsvc/Names.hpp>
//#include <comfw/Interfaces.hpp>
#include <thread>
#include <iostream>
#include <fstream>

using namespace std::chrono_literals;
using std::chrono::seconds;
using std::chrono::minutes;

using OptionalAcpType = boost::optional<xsd::m2m::AcpType>;

/*
std::ostream& operator<<( std::ostream& os, m2m::ResponsePrimitive_ptr const response )
{
	std::stringstream ss;
	ss << "rsc: " << getResponseStatusCodeName( *response->responseStatusCode ) << '\n';
	if( response->primitiveContent.isSet() )
	{
		auto& any = response->primitiveContent->getAny();
		ss << any.dumpJson( 4 ) << '\n';
	}
	else
	{
		ss << "<no primitive content>\n";
	}
	os << ss.str();
	return os;
}
*/

//std::ostream& operator<<( std::ostream& os, m2m::ResponsePrimitive_ptr response )
//{
//	//return os << toString( response );
//	return os;
//}

class FloatingNeutral : public m2m::AppEntity
{
	bool haveLast_{ false };
	float lastVoltage_{ 0.0f };
	float lastCurrent_{ 0.0f };

	float currentThreshold_{ 8.0f };
	float varianceThreshold_{ 0.7f };
	bool thresholdsCurrentlyExceeded_{ false };
	bool disconnectService_{ false };

    m2m::ResponsePrimitive_ptr createContainer( std::string const& parent,
		std::string const& name, OptionalAcpType const& acpType = {} )
    {
        auto container = xsd::m2m::Container::Create();
        container.creator = getAeId();
        container.resourceName = name;
        if( acpType )
        {
            container.accessControlPolicyIDs.assign( *acpType );
        }

        m2m::Request request = newRequest( m2m::Operation::Create, m2m::To{ parent } );
        request.req->resourceType = xsd::m2m::ResourceType::container;
        request.req->resultContent = xsd::m2m::ResultContent::Attributes;
        request.req->primitiveContent = xsd::toAnyNamed( container );

        sendRequest( request );
        return getResponse( request );
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

	bool create_subscription()
	{
		auto subscription = xsd::m2m::Subscription::Create();
		subscription.creator = std::string();

		// set the subscription's name
		//subscription.resourceName = "metersv-samp1-sub-01";
		subscription.resourceName = "metersv-" + getResourceId();

		// have all resource attributes be provided in the notifications
		subscription.notificationContentType = xsd::m2m::NotificationContentType::all_attributes;

		// set the notification destination to be this AE.
		subscription.notificationURI = xsd::m2m::ListOfURIs();
		subscription.notificationURI->push_back( getResourceId() );

		// set eventNotificationCriteria to creation of child resources
		xsd::m2m::EventNotificationCriteria eventNotificationCriteria;
		eventNotificationCriteria.notificationEventType.assign()
			.push_back( xsd::m2m::NotificationEventType::Create_of_Direct_Child_Resource );
		subscription.eventNotificationCriteria = std::move(eventNotificationCriteria);

		auto request = newRequest( xsd::m2m::Operation::Create, m2m::To{"./metersvc/reads"} );
		request.req->resultContent = xsd::m2m::ResultContent::Nothing;
		request.req->resourceType = xsd::m2m::ResourceType::subscription;
		request.req->primitiveContent = xsd::toAnyNamed(subscription);

		sendRequest( request );
		auto response = getResponse( request );
		auto const& statusCode = response->responseStatusCode;

		logInfo( "subscription: " << statusCode );

		bool created = statusCode == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict =  statusCode == xsd::m2m::ResponseStatusCode::CONFLICT;

		return created or conflict;
	}

	bool create_meter_read_policy( char const* readingType, unsigned recurrencePeriod, std::string const& polId )
	{
		xsd::mtrsvc::ScheduleInterval scheduleInterval;
		scheduleInterval.end = nullptr;
		scheduleInterval.start = "2020-06-19T00:00:00";

		xsd::mtrsvc::TimeSchedule timeSchedule;
		timeSchedule.recurrencePeriod = recurrencePeriod;
		timeSchedule.scheduleInterval = std::move(scheduleInterval);

		xsd::mtrsvc::MeterReadSchedule meterReadSchedule;
		meterReadSchedule.readingType = readingType;
		meterReadSchedule.timeSchedule = std::move(timeSchedule);

		xsd::mtrsvc::MeterServicePolicy meterServicePolicy;
		meterServicePolicy = std::move(meterReadSchedule);

		xsd::m2m::ContentInstance policyInst = xsd::m2m::ContentInstance::Create();
		policyInst.content = xsd::toAnyTypeUnnamed(meterServicePolicy);

		//policyInst.resourceName = "metersvc-sampl-pol-" + polId;
		policyInst.resourceName = "metersv-" + getResourceId();

		auto request = newRequest( xsd::m2m::Operation::Create, m2m::To{ "./metersvc/policies" } );
		request.req->resultContent = xsd::m2m::ResultContent::Nothing;
		request.req->resourceType = xsd::m2m::ResourceType::contentInstance;
		request.req->primitiveContent = xsd::toAnyNamed( policyInst );

		sendRequest( request );
		auto response = getResponse( request );
		auto const& statusCode = response->responseStatusCode;

		logInfo( readingType << " policy creation: " << statusCode );

		bool created = statusCode == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict = statusCode == xsd::m2m::ResponseStatusCode::CONFLICT;

		return created or conflict;
	}

	bool create_meter_read_pq_policy()
	{
    	return create_meter_read_policy( "powerQuality", 120, "01" );
	}

	//bool create_meter_read_summations_policy()
	//{
	//	return true;
	//}

	bool prepare()
	{
		activate();

		if( not create_subscription() )
		{
			logError( "subscription creation failed" );
			return false;
		}

		if( not create_meter_read_pq_policy() )
		{
			logError( "power-quality meter read policy creation failed" );
			return false;
		}

		//if( not create_meter_read_summations_policy() )
		//{
		//	logError( "summations meter read policy creation failed" );
		//	return false;
		//}

		return true;
	}

	void notificationCallback( m2m::Notification notification )
	{
		auto & event = notification.notificationEvent;
		if( not event.isSet() )
		{
			logWarn( "notification has no notificationEvent" );
			return;
		}

		auto & eventType = event->notificationEventType;

		// TODO make debug
		logInfo( "got notification type " << toString( eventType) );

		if( eventType != xsd::m2m::NotificationEventType::Create_of_Direct_Child_Resource )
		{
			return;
		}

		auto contentInstance = event->representation->extractNamed< xsd::m2m::ContentInstance >();
		auto meterRead = contentInstance.content->extractUnnamed< xsd::mtrsvc::MeterRead >();

		auto const& meterSvcData = *(meterRead.meterSvcData);
		auto const& timestamp = meterSvcData.readTimeLocal;
		auto const& powerQuality = meterSvcData.powerQuality;

		if( not powerQuality.isSet() )
		{
			return;
		}

		logInfo( "timestamp: " << timestamp );
		logInfo( "powerQuality: " << *powerQuality );

		//std::ofstream output( "powerquality_meter_data.txt" );
		//output << "timestamp: " << timestamp;
		//output << "powerQuality:\n" << *powerQuality;

		if( not powerQuality->voltageA.isSet() or not powerQuality->currentA.isSet() )
		{
			return;
		}

		float voltage = *powerQuality->voltageA;
		float current = *powerQuality->currentA;

		bool thresholdsExceeded = false;
		float deltaV = 0;
		float deltaI = 0;

		if( haveLast_ )
		{
			deltaV = std::abs( voltage - lastVoltage_ );
			deltaI = std::abs( current - lastCurrent_ );
			float variance = deltaV / deltaI;

			if( deltaI > currentThreshold_ and variance > varianceThreshold_ )
			{
				thresholdsExceeded = true;
			}
		}

		lastVoltage_ = voltage;
		lastCurrent_ = current;
		haveLast_ = true;

		if( thresholdsExceeded and not thresholdsCurrentlyExceeded_ )
		{
			logWarn( "loss-of-neutral thresholds exceeded: deltaI=" << deltaI << ", deltaV=" << deltaV );
			unsigned errorReason = 0;
			if( disconnectService_ )
			{
#if 0
				auto meterOperationService = comfw::Interfaces::get< MeterOperationServiceInterface >();
				auto& loadControl = meterOperationService->getMeterLoadControl();
				if( not loadControl->energyServiceIsDisabled() )
				{
					logWarn( "disconnecting energy service" );
					try
					{
						loadControl()->disableEnergyService();
					}
					catch( MeterDisableException& e )
					{
						logWarn( "Exception: " << e.what() );
						errorReason = e.getReason();
					}
				}
#endif
			}

//			ostringstream arg;
//			arg << "Loss Of Neutral: "
//				<< "deltaV=" << deltaV
//				<< ", "
//				<< "deltaI=" << deltaI;

//			publishReport(EventCodes::GnLossOfNeutralAlarm::Index, arg.str());
//			if (errorReason & MeterDisableException::SWITCH_OPEN_FAILED)
//			{
//				publishReport(EventCodes::GnRcdcSwitchOpenFailure::Index, "switch open failed during energy service disable");
//			}
//			if (errorReason & MeterDisableException::DISABLE_FAILED)
//			{
//				publishReport(EventCodes::GnServiceSwitchDisableFailure::Index, "failed to disable energy service");
//			}
		}
		else if( not thresholdsExceeded and thresholdsCurrentlyExceeded_ )
		{
			logInfo( "loss-of-neutral: DeltaV/DeltaI thresholds returned to normal" );
		}

		thresholdsCurrentlyExceeded_ = thresholdsExceeded;
	}

public:
    FloatingNeutral( std::string const& config )
		//: AppEntity( config, std::bind( &FloatingNeutral::notificationCallback, this, std::placeholders::_1 ) )
		: AppEntity( std::bind( &FloatingNeutral::notificationCallback, this, std::placeholders::_1 ) )
    {
    }

	void run()
	{
		while( not prepare() )
		{
			std::this_thread::sleep_for( seconds( 30 ) );
		}

		//auto rsp = createContainer( ".", "foocontainer" );
		//if( *rsp->responseStatusCode != xsd::m2m::ResponseStatusCode::CREATED )
		//{
		//	logError( "could not create container: " << rsp );
		//	//TODO retry
		//	exit( -1 );
		//}

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

	FloatingNeutral app( "aos_neutral_app.config" );
	app.run();

	return 0;
}
