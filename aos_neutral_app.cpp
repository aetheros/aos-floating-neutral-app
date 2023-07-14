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
#include <thread>
#include <iostream>
#include <fstream>
//#include <iomanip>

using namespace std::chrono_literals;
using std::chrono::seconds;
using std::chrono::minutes;

//using time_point = std::chrono::system_clock::time_point;
using time_point = std::chrono::time_point< std::chrono::system_clock, std::chrono::seconds >;

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

//======================================================

struct DSO_API FloatingNeutralConfig : public xsd::xs::ComplexType
{
	typedef xsd::xs::ComplexType base_type;

	FloatingNeutralConfig()
	{
		//samplingPeriod = 60;
		samplingPeriod = 10;
		currentThreshold = 8.0f;
		varianceThreshold = 0.7f;
		//alarmContainer = "/PN_CSE/policynet.m2m/cntAosFloatingNeutral";
		alarmContainer = "./alarms";
		disconnectService = false;
	}
	~FloatingNeutralConfig() override = default;

	FloatingNeutralConfig( FloatingNeutralConfig && ) = default;
	FloatingNeutralConfig( FloatingNeutralConfig const& ) = default;
	FloatingNeutralConfig& operator=( FloatingNeutralConfig && ) = default;
	FloatingNeutralConfig& operator=( FloatingNeutralConfig const& ) = default;

	//static const char * const qualifiedShortName; //!< The qualified short name ('m2m:dmd')
	//const char *getQualifiedShortName() const override; //!< Get the qualified short name

	bool unmarshallMember( xsd::Unmarshaller& m, char const* memberName ) override //!< unmarshalls one member element
	{
		if( base_type::unmarshallMember( m, memberName ) )
		{
			return true;
		}
		else if( std::strcmp( memberName, "samplingPeriod" ) == 0 )
		{
			m >> samplingPeriod;
			return true;
		}
		else if( std::strcmp( memberName, "currentThreshold" ) == 0 )
		{
			m >> currentThreshold;
			return true;
		}
		else if( std::strcmp( memberName, "varianceThreshold" ) == 0 )
		{
			m >> varianceThreshold;
			return true;
		}
		else if( std::strcmp( memberName, "alarmContainer" ) == 0 )
		{
			m >> alarmContainer;
			return true;
		}
		else if( std::strcmp( memberName, "disconnectService" ) == 0 )
		{
			m >> disconnectService;
			return true;
		}
		return false;
	}

	void marshallMembers( xsd::Marshaller& m ) const override //!< marshalls all member element
	{
		base_type::marshallMembers( m );
		m << xsd::member( "samplingPeriod" ) << samplingPeriod;
		m << xsd::member( "currentThreshold" ) << currentThreshold;
		m << xsd::member( "varianceThreshold" ) << varianceThreshold;
		m << xsd::member( "alarmContainer" ) << alarmContainer;
		m << xsd::member( "disconnectService" ) << disconnectService;
		return;
	}

	xsd::xs::Element<xsd::xs::Integer, 0> samplingPeriod{ "samplingPeriod" };
	xsd::xs::Element<xsd::xs::Float, 0> currentThreshold{ "currentThreshold" };
	xsd::xs::Element<xsd::xs::Float, 0> varianceThreshold{ "varianceThreshold" };
	xsd::xs::Element<xsd::xs::String, 0> alarmContainer{ "alarmContainer" };
	xsd::xs::Element<xsd::xs::Boolean, 0> disconnectService{ "disconnectService" };
};

//======================================================

class ExtendedAppEntity : public m2m::AppEntity
{
public:
	ExtendedAppEntity( NotificationCallback cb )
		: AppEntity( cb )
    {
    }

    m2m::ResponsePrimitive_ptr createContainer( std::string const& parent,
		std::string const& name, OptionalAcpType const& acpType = {} )
    {
        auto container = xsd::m2m::Container::Create();
        container.creator.assign();
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

	m2m::ResponsePrimitive_ptr createContentInstance( std::string const& container, std::string const& name,
		xsd::xs::AnyType const& content ) //std::string const& content )
	{
		auto ci = xsd::m2m::ContentInstance::Create();
		ci.creator.assign();
		ci.resourceName = name;
		ci.content = content; //xsd::toAnyTypeUnnamed( content );

		m2m::Request request = newRequest( m2m::Operation::Create, m2m::To{ container } );
		request.req->resourceType = xsd::m2m::ResourceType::contentInstance;
		request.req->resultContent = xsd::m2m::ResultContent::Attributes;
		request.req->primitiveContent = xsd::toAnyNamed( ci );

		sendRequest( request );
		return getResponse( request );
	}

	m2m::ResponsePrimitive_ptr createSimpleSubscription( std::string const& target, std::string const& name,
		xsd::m2m::EventNotificationCriteria eventNotificationCriteria )
	{
		auto subscription = xsd::m2m::Subscription::Create();
		subscription.creator = std::string();

		// set the subscription's name
		subscription.resourceName = name;

		// have all resource attributes be provided in the notifications
		subscription.notificationContentType = xsd::m2m::NotificationContentType::all_attributes;

		// set the notification destination to be this AE.
		subscription.notificationURI = xsd::m2m::ListOfURIs();
		subscription.notificationURI->push_back( getResourceId() );

		// set eventNotificationCriteria to creation of child resources
		subscription.eventNotificationCriteria = std::move( eventNotificationCriteria );

		auto request = newRequest( xsd::m2m::Operation::Create, m2m::To{ target } );
		request.req->resourceType = xsd::m2m::ResourceType::subscription;
		request.req->resultContent = xsd::m2m::ResultContent::Nothing;
		request.req->primitiveContent = xsd::toAnyNamed( subscription );

		sendRequest( request );
		return getResponse( request );
	}

	m2m::ResponsePrimitive_ptr retrieveResource( std::string const& path )
	{
		m2m::Request request = newRequest( m2m::Operation::Retrieve, m2m::To{ path } );
		sendRequest( request );
		return getResponse( request );
	}

	m2m::ResponsePrimitive_ptr deleteResource( std::string const& path )
	{
		m2m::Request request = newRequest( m2m::Operation::Delete, m2m::To{ path } );
		request.req->resultContent = xsd::m2m::ResultContent::Attributes;
		sendRequest( request );
		return getResponse( request );
	}
};

//======================================================

class FloatingNeutral : public ExtendedAppEntity
{
	std::string const cRootContainerName = "app-config";
	std::string const cRootContainerPath = std::string( "./" ) + cRootContainerName;
	std::string const cContainerPath = cRootContainerPath + '/' + getAppName();
	std::string const cPath = cContainerPath + "/config";

	FloatingNeutralConfig config_;

	bool thresholdsCurrentlyExceeded_{ false };
	bool haveLast_{ false };
	time_point lastTimestamp_;
	float lastVoltage_{ 0.0f };
	float lastCurrent_{ 0.0f };

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

	bool ensureContainer( std::string const& parent, std::string const& name,
		OptionalAcpType const& acpType = {} )
	{
		auto rsp = createContainer( parent, name, acpType );
		auto const& status = rsp->responseStatusCode;
		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict = status == xsd::m2m::ResponseStatusCode::CONFLICT;
		return created or conflict;
	}

	bool ensureDefaults()
	{
		FloatingNeutralConfig defaultConfig;
		auto anyConfig = xsd::toAnyTypeUnnamed( defaultConfig );
		auto rsp = createContentInstance( cContainerPath, "config", anyConfig );
		auto const& status = rsp->responseStatusCode;
		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict = status == xsd::m2m::ResponseStatusCode::CONFLICT;
		return created or conflict;
	}

	bool ensureConfigSubscription()
	{
		xsd::m2m::EventNotificationCriteria eventNotificationCriteria;
		eventNotificationCriteria.notificationEventType.assign()
			.push_back( xsd::m2m::NotificationEventType::Create_of_Direct_Child_Resource );
		auto rsp = createSimpleSubscription( cContainerPath, "config-" + getResourceId(),
			eventNotificationCriteria );
		auto const& status = rsp->responseStatusCode;
		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict = status == xsd::m2m::ResponseStatusCode::CONFLICT;
		return created or conflict;
	}

	bool createPowerQualitySubscription()
	{
		// set eventNotificationCriteria to creation of child resources
		xsd::m2m::EventNotificationCriteria eventNotificationCriteria;
		eventNotificationCriteria.notificationEventType.assign()
			.push_back( xsd::m2m::NotificationEventType::Create_of_Direct_Child_Resource );

		auto rsp = createSimpleSubscription( "./metersvc/reads", "metersv-" + getResourceId(),
			eventNotificationCriteria );
		auto const& statusCode = rsp->responseStatusCode;

		logInfo( "subscription: " << statusCode );

		bool created = statusCode == xsd::m2m::ResponseStatusCode::CREATED;
		bool conflict =  statusCode == xsd::m2m::ResponseStatusCode::CONFLICT;

		return created or conflict;
	}

	bool createMeterReadPowerQualityPolicy()
	{
		// policy attributes can change, so delete whatever is there first
		deleteResource( "./metersvc/policies/" + getResourceId() + "-metersv-pol" );

		xsd::mtrsvc::ScheduleInterval scheduleInterval;
		scheduleInterval.end = nullptr;
		scheduleInterval.start = "2020-06-19T00:00:00";

		xsd::mtrsvc::TimeSchedule timeSchedule;
		timeSchedule.recurrencePeriod = *config_.samplingPeriod;
		timeSchedule.scheduleInterval = std::move(scheduleInterval);

		xsd::mtrsvc::MeterReadSchedule meterReadSchedule;
		meterReadSchedule.readingType = "powerQuality";
		meterReadSchedule.timeSchedule = std::move(timeSchedule);

		xsd::mtrsvc::MeterServicePolicy meterServicePolicy;
		meterServicePolicy = std::move(meterReadSchedule);

		auto response = createContentInstance( "./metersvc/policies", getResourceId() + "-metersv-pol",
			xsd::toAnyTypeUnnamed( meterServicePolicy ) );
		auto const& status = response->responseStatusCode;

		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;

		return created;
	}

	bool prepare_config()
	{
		//TODO remove these deletes, maybe err if no 'app-config' container instead of creating it
		auto delRsp = deleteResource( cPath );
		logInfo( "delRsp: " << *delRsp );

		delRsp = deleteResource( cContainerPath );
		logInfo( "delRsp2: " << *delRsp );

		delRsp = deleteResource( cRootContainerPath );
		logInfo( "delRsp3: " << *delRsp );

		//---------------------------------------------------------------------------

		if( not ensureContainer( ".", cRootContainerName ) )
		{
			logError( "could not ensure container: " << cRootContainerPath );
			return false;
		}

		if( not ensureContainer( cRootContainerPath, getAppName() ) )
		{
			logError( "could not ensure container: " << cContainerPath );
			return false;
		}

		if( not ensureConfigSubscription() )
		{
			logError( "could not ensure config subscription" );
			return false;
		}

		//---------------------------------------------------------------------------

		auto retConfigRsp = retrieveResource( cPath );
		auto retConfigStatus = retConfigRsp->responseStatusCode;

		if( retConfigStatus == xsd::m2m::ResponseStatusCode::OK )
		{
			auto contentInstance = retConfigRsp->primitiveContent->any.extractNamed< xsd::m2m::ContentInstance >();
			auto config_ = contentInstance.content->extractUnnamed< FloatingNeutralConfig >();
			sanitize( config_ );
		}
		else if( retConfigStatus == xsd::m2m::ResponseStatusCode::NOT_FOUND )
		{
			// nothing to do here, just use FloatingNeutralConfig's default values
		}
		else
		{
			logError( "not able to retrieve config: " << *retConfigRsp );
			return false;
		}

		return true;
	}

	bool prepare()
	{
		activate();

		if( not prepare_config() )
		{
			logError( "could not prepare config" );
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

		return true;
	}

	void notificationCallback( m2m::Notification notification )
	{
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
		auto configSubName = std::string( "config-" ) + getResourceId();
		auto meterSubName = std::string( "metersv-" ) + getResourceId();

		if( subref.find( configSubName ) != std::string::npos )
		{
			//logInfo( "got config notification" );
			auto contentInstance = event->representation->extractNamed< xsd::m2m::ContentInstance >();
			auto newConfig = contentInstance.content->extractUnnamed< FloatingNeutralConfig >();
			processNewConfig( std::move( newConfig ) );
		}
		else if( subref.find( meterSubName ) != std::string::npos )
		{
			//logInfo( "got metersv notification" );
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

	void processNewConfig( FloatingNeutralConfig&& newConfig )
	{
		sanitize( newConfig );
		config_ = std::move( newConfig );
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
			3600*1, 3600*2, 3600*3, 3600*4, 3600*5, 3600*6, 3600*8, 3600*12, 3600*24, // valid hours
		};
		uint32_t requestedPeriod = *newConfig.samplingPeriod;
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
			if( deltaT < 0.9 * *config_.samplingPeriod )
			{
				logInfo( "power quality update too early, ignoring" );
				return;
			}
			if( deltaT > 1.1 * *config_.samplingPeriod )
			{
				logInfo( "power quality update too late, skipping check" );
			}
			else
			{
				deltaV = std::abs( voltage - lastVoltage_ );
				deltaI = std::abs( current - lastCurrent_ );
				float variance = deltaV / deltaI;

				if( deltaI > *config_.currentThreshold and variance > *config_.varianceThreshold )
				{
					thresholdsExceeded = true;
				}
			}
		}

		lastTimestamp_ = thisTimestamp;
		lastVoltage_ = voltage;
		lastCurrent_ = current;
		haveLast_ = true;

		if( thresholdsExceeded and not thresholdsCurrentlyExceeded_ )
		{
			logWarn( "loss-of-neutral thresholds exceeded: deltaI=" << deltaI << ", deltaV=" << deltaV );

			if( *config_.disconnectService )
			{
				disableEnergyService();
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
		auto rsp = deleteResource( *config_.alarmContainer + "/loss-of-neutral-alarm" );
		auto status = rsp->responseStatusCode;
		auto deleted = status == xsd::m2m::ResponseStatusCode::DELETED;
		auto not_found = status == xsd::m2m::ResponseStatusCode::NOT_FOUND;

		if( not deleted and not not_found )
		{
			logError( "could not retract alarm: " << *rsp );
		}
	}

	void publishAlarm( std::string const& text )
	{
		retractAlarm();

		auto rsp = createContentInstance( *config_.alarmContainer, "loss-of-neutral-alarm", xsd::toAnyTypeUnnamed( text ) );
		auto status = rsp->responseStatusCode;
		bool created = status == xsd::m2m::ResponseStatusCode::CREATED;

		if( not created )
		{
			logError( "could not publish alarm" << *rsp );
		}
	}

public:
    FloatingNeutral()
		: ExtendedAppEntity( std::bind( &FloatingNeutral::notificationCallback, this, std::placeholders::_1 ) )
    {
    }

	void run()
	{
		while( not prepare() )
		{
			std::this_thread::sleep_for( seconds( 30 ) );
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
