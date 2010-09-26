/* 
 *  Copyright (c) 2010,
 *  Gavriloaie Eugen-Andrei (shiretu@gmail.com)
 *
 *  This file is part of crtmpserver.
 *  crtmpserver is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  crtmpserver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with crtmpserver.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "common.h"
#include "application/baseclientapplication.h"
#include "application/baseappprotocolhandler.h"
#include "protocols/baseprotocol.h"
#include "streaming/basestream.h"

uint32_t BaseClientApplication::_idGenerator = 0;

BaseClientApplication::BaseClientApplication(Variant &configuration)
: _streamsManager(this) {
	_id = _idGenerator++;
	_configuration = configuration;
	_name = (string) configuration[CONF_APPLICATION_NAME];
	if ((VariantType) configuration[CONF_APPLICATION_ALIASES] != V_NULL) {

		FOR_MAP((configuration[CONF_APPLICATION_ALIASES]), string, Variant, i) {
			ADD_VECTOR_END(_aliases, MAP_VAL(i));
		}
	}
	_isDefault = (VariantType) configuration[CONF_APPLICATION_DEFAULT] != V_NULL ?
			(bool)configuration[CONF_APPLICATION_DEFAULT] : false;
}

BaseClientApplication::~BaseClientApplication() {

}

uint32_t BaseClientApplication::GetId() {
	return _id;
}

string BaseClientApplication::GetName() {
	return _name;
}

Variant BaseClientApplication::GetConfiguration() {
	return _configuration;
}

vector<string> BaseClientApplication::GetAliases() {
	return _aliases;
}

bool BaseClientApplication::IsDefault() {
	return _isDefault;
}

StreamsManager *BaseClientApplication::GetStreamsManager() {
	return &_streamsManager;
}

bool BaseClientApplication::Initialize() {
	return true;
}

void BaseClientApplication::RegisterAppProtocolHandler(uint64_t protocolType,
		BaseAppProtocolHandler *pAppProtocolHandler) {
	if (MAP_HAS1(_protocolsHandlers, protocolType))
		ASSERT("Invalid protocol handler type. Already registered");
	_protocolsHandlers[protocolType] = pAppProtocolHandler;
	pAppProtocolHandler->SetApplication(this);
}

void BaseClientApplication::UnRegisterAppProtocolHandler(uint64_t protocolType) {
	if (MAP_HAS1(_protocolsHandlers, protocolType))
		_protocolsHandlers[protocolType]->SetApplication(NULL);
	_protocolsHandlers.erase(protocolType);
}

BaseAppProtocolHandler *BaseClientApplication::GetProtocolHandler(BaseProtocol *pProtocol) {
	return GetProtocolHandler(pProtocol->GetType());
}

BaseAppProtocolHandler *BaseClientApplication::GetProtocolHandler(uint64_t protocolType) {
	if (!MAP_HAS1(_protocolsHandlers, protocolType)) {
		FINEST("protocolType: %llu", protocolType);

		FOR_MAP(_protocolsHandlers, uint64_t, BaseAppProtocolHandler *, i) {
			FINEST("%llu: %p", MAP_KEY(i), MAP_VAL(i));
		}
		ASSERT("Protocol handler not activated for protocol type %d in application %s",
				protocolType, STR(_name));
	}
	return _protocolsHandlers[protocolType];
}

BaseAppProtocolHandler *BaseClientApplication::GetProtocolHandler(string &scheme) {
	BaseAppProtocolHandler *pResult = NULL;
	if (false) {

	}
#ifdef HAS_PROTOCOL_RTMP
	else if (scheme.find("rtmp") == 0) {
		pResult = GetProtocolHandler(PT_INBOUND_RTMP);
		if (pResult == NULL)
			pResult = GetProtocolHandler(PT_OUTBOUND_RTMP);
	}
#endif /* HAS_PROTOCOL_RTMP */
#ifdef HAS_PROTOCOL_RTP
	else if (scheme == "rtsp") {
		pResult = GetProtocolHandler(PT_RTSP);
	}
#endif /* HAS_PROTOCOL_RTP */
	else {
		WARN("scheme %s not recognized", STR(scheme));
	}
	return pResult;
}

void BaseClientApplication::RegisterProtocol(BaseProtocol *pProtocol) {
	if (!MAP_HAS1(_protocolsHandlers, pProtocol->GetType()))
		ASSERT("Protocol handler not activated for protocol type %s in application %s",
			STR(tagToString(pProtocol->GetType())),
			STR(_name));
	_protocolsHandlers[pProtocol->GetType()]->RegisterProtocol(pProtocol);
}

void BaseClientApplication::UnRegisterProtocol(BaseProtocol *pProtocol) {
	if (!MAP_HAS1(_protocolsHandlers, pProtocol->GetType()))
		ASSERT("Protocol handler not activated for protocol type %d in application %s",
			pProtocol->GetType(), STR(_name));
	_streamsManager.UnRegisterStreams(pProtocol->GetId());
	_protocolsHandlers[pProtocol->GetType()]->UnRegisterProtocol(pProtocol);
	FINEST("Protocol %s unregistered from application: %s", STR(*pProtocol), STR(_name));
}

void BaseClientApplication::SignalStreamRegistered(BaseStream *pStream) {
	INFO("Stream %d of type %s with name `%s` registered to application `%s`",
			pStream->GetUniqueId(),
			STR(tagToString(pStream->GetType())),
			STR(pStream->GetName()),
			STR(_name));
}

void BaseClientApplication::SignalStreamUnRegistered(BaseStream *pStream) {
	INFO("Stream %d of type %s with name `%s` unregistered from application `%s`",
			pStream->GetUniqueId(),
			STR(tagToString(pStream->GetType())),
			STR(pStream->GetName()),
			STR(_name));
}

bool BaseClientApplication::PullExternalStreams() {
	//1. Minimal verifications
	if (_configuration["externalStreams"] == V_NULL) {
		return true;
	}

	if (_configuration["externalStreams"] != V_MAP) {
		FATAL("Invalid rtspStreams node");
		return false;
	}

	//2. Loop over the stream definitions and spawn the streams

	FOR_MAP(_configuration["externalStreams"], string, Variant, i) {
		Variant &streamConfig = MAP_VAL(i);
		if (streamConfig != V_MAP) {
			WARN("External stream configuration is invalid:\n%s",
					STR(streamConfig.ToString()));
			continue;
		}
		if (!PullExternalStream(streamConfig)) {
			WARN("External stream configuration is invalid:\n%s",
					STR(streamConfig.ToString()));
		}
	}

	//3. Done
	return true;
}

bool BaseClientApplication::PullExternalStream(Variant streamConfig) {
	//1. Minimal verification
	if (streamConfig["uri"] != V_STRING) {
		FATAL("Invalid uri");
		return false;
	}

	//2. Split the URI
	URI uri;
	if (!URI::FromString(streamConfig["uri"], true, uri)) {
		FATAL("Invalid URI: %s", STR(streamConfig["uri"].ToString()));
		return false;
	}
	streamConfig["uri"] = uri.ToVariant();

	//3. Depending on the scheme name, get the curresponding protocol handler
	///TODO: integrate this into protocol factory manager via protocol factories
	BaseAppProtocolHandler *pProtocolHandler = GetProtocolHandler(uri.scheme);
	if (pProtocolHandler == NULL) {
		WARN("Unable to find protocol handler for scheme %s in application %s",
				STR(uri.scheme),
				STR(GetName()));
		return false;
	}

	//4. Initiate the stream pulling sequence
	return pProtocolHandler->PullExternalStream(uri, streamConfig);
}

