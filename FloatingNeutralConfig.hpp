// vim: sw=4 expandtab
// Copyright (c) Aetheros, Inc.  See COPYRIGHT

#pragma once

#include <aos/Log.hpp>
#include <m2m/AppEntity.hpp>

struct DSO_API FloatingNeutralConfig : public xsd::xs::ComplexType
{
	typedef xsd::xs::ComplexType base_type;

	FloatingNeutralConfig()
	{
		//samplingPeriod = 60;
		samplingPeriod = 30; // for test
		currentThreshold = 8.0f;
		varianceThreshold = 0.7f;
		//alarmContainer = "/PN_CSE/policynet.m2m/cntAosFloatingNeutral";
		alarmContainer = "./alarms"; // for test
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
