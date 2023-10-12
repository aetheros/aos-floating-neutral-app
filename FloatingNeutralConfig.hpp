// vim: sw=4 expandtab
// Copyright (c) Aetheros, Inc.  See COPYRIGHT

#pragma once

#include <aos/Log.hpp>
#include <nlohmann/json.hpp>

struct DSO_API FloatingNeutralConfig
{
	uint32_t samplingPeriod{ 60 };
	float currentThreshold{ 8.0f };
	float varianceThreshold{ 0.7f };
	std::string alarmContainer{ "/PN_CSE/policynet.m2m/cntAosFloatingNeutral" };
	bool disconnectService{ false };

	FloatingNeutralConfig() = default;
	FloatingNeutralConfig( std::string const& jsonConfig )
	{
		using json = nlohmann::json;

		json config = json::parse( jsonConfig );

		if( config.count( "samplingPeriod" ) )
		{
			samplingPeriod = config["samplingPeriod"].template get<uint32_t>();
		}
		if( config.count( "currentThreshold" ) )
		{
			currentThreshold = config["currentThreshold"].template get<float>();
		}
		if( config.count( "varianceThreshold" ) )
		{
			varianceThreshold = config["varianceThreshold"].template get<float>();
		}
		if( config.count( "alarmContainer" ) )
		{
			alarmContainer = config["alarmContainer"].template get<std::string>();
		}
		if( config.count( "disconnectService" ) )
		{
			disconnectService = config["disconnectService"].template get<bool>();
		}
	}

	std::string dump()
	{
		nlohmann::json config;
		config[ "samplingPeriod" ] = samplingPeriod;
		config[ "currentThreshold" ] = currentThreshold;
		config[ "varianceThreshold" ] = varianceThreshold;
		config[ "alarmContainer" ] = alarmContainer;
		config[ "disconnectService" ] = disconnectService;
		return config.dump();
	}

	~FloatingNeutralConfig() = default;

	FloatingNeutralConfig( FloatingNeutralConfig && ) = default;
	FloatingNeutralConfig( FloatingNeutralConfig const& ) = default;
	FloatingNeutralConfig& operator=( FloatingNeutralConfig && ) = default;
	FloatingNeutralConfig& operator=( FloatingNeutralConfig const& ) = default;
};
