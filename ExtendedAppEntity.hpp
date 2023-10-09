// vim: sw=4 expandtab
// Copyright (c) Aetheros, Inc.  See COPYRIGHT

#pragma once

#include <m2m/AppEntity.hpp>

using OptionalAcpType = boost::optional<xsd::m2m::AcpType>;
using OptionalMaxInstances = boost::optional<uint32_t>;

class ExtendedAppEntity : public m2m::AppEntity
{
public:
	ExtendedAppEntity( NotificationCallback cb )
		: AppEntity( cb )
    {
    }

    m2m::ResponsePrimitive_ptr createContainer( std::string const& parent,
		std::string const& name, OptionalAcpType const& acpType = {}, OptionalMaxInstances maxNrOfInstances = {} )
    {
        auto container = xsd::m2m::Container::Create();
        container.creator.assign();
        container.resourceName = name;
        if( acpType )
        {
            container.accessControlPolicyIDs.assign( *acpType );
        }
		if( maxNrOfInstances )
		{
			container.maxNrOfInstances = *maxNrOfInstances;
		}

        m2m::Request request = newRequest( m2m::Operation::Create, m2m::To{ parent } );
        request.req->resourceType = xsd::m2m::ResourceType::container;
        request.req->resultContent = xsd::m2m::ResultContent::Nothing;
        request.req->primitiveContent = xsd::toAnyNamed( container );

        sendRequest( request );
        return getResponse( request );
    }

	m2m::ResponsePrimitive_ptr createContentInstance( std::string const& container, std::string const& name,
		xsd::xs::AnyType const& content )
	{
		auto ci = xsd::m2m::ContentInstance::Create();
		ci.creator.assign();
		ci.resourceName = name;
		ci.content = content;

		m2m::Request request = newRequest( m2m::Operation::Create, m2m::To{ container } );
		request.req->resourceType = xsd::m2m::ResourceType::contentInstance;
		request.req->resultContent = xsd::m2m::ResultContent::Nothing;
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
		request.req->resultContent = xsd::m2m::ResultContent::Nothing;
		sendRequest( request );
		return getResponse( request );
	}
};
