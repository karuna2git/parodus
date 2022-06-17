/**
 * Copyright 2021 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
/**
 * @file upstream_rbus.c
 *
 * @description This is used for parodus-RBUS communication 
 * to send notification events upstream to cloud.
 *
 */

#include <stdlib.h>
#include <rbus.h>
#include "upstream.h"
#include "ParodusInternal.h"
#include "partners_check.h"
#include "close_retry.h"
#include "connection.h"
#include "heartBeat.h"

#define WEBCFG_UPSTREAM_EVENT "Webconfig.Upstream"
#ifdef WAN_FAILOVER_SUPPORTED
#define WEBPA_INTERFACE "Device.X_RDK_WanManager.CurrentActiveInterface"
#endif

rbusHandle_t rbus_Handle;
rbusError_t err;

void processWebconfigUpstreamEvent(rbusHandle_t handle, rbusEvent_t const* event, rbusEventSubscription_t* subscription);

void subscribeAsyncHandler( rbusHandle_t handle, rbusEventSubscription_t* subscription, rbusError_t error);

rbusHandle_t get_parodus_rbus_Handle(void)
{
     return rbus_Handle;
}
#ifdef WAN_FAILOVER_SUPPORTED
void eventReceiveHandler( rbusHandle_t rbus_Handle, rbusEvent_t const* event, rbusEventSubscription_t* subscription );
#endif

/* API to register RBUS listener to receive messages from webconfig */
void subscribeRBUSevent()
{
    int rc = RBUS_ERROR_SUCCESS;
	err = rbus_open(&rbus_Handle, "parodus");
	if (err)
	{
		ParodusError("rbus_open failed :%s\n", rbusError_ToString(err));
		return;
	}
    rc = rbusEvent_SubscribeAsync(rbus_Handle,WEBCFG_UPSTREAM_EVENT,processWebconfigUpstreamEvent,subscribeAsyncHandler,"parodus",10*60);
    if(rc != RBUS_ERROR_SUCCESS)
        ParodusError("rbusEvent_Subscribe failed: %d, %s\n", rc, rbusError_ToString(rc));
    else
        ParodusInfo("rbusEvent_Subscribe was successful\n");
}

#ifdef WAN_FAILOVER_SUPPORTED
/* API to subscribe Active Interface name on value change event*/
int subscribeCurrentActiveInterfaceEvent()
{
	int rc = RBUS_ERROR_SUCCESS;
	ParodusPrint("Subscribing to Device.X_RDK_WanManager.CurrentActiveInterface Event\n");
	rc = rbusEvent_SubscribeAsync(rbus_Handle,WEBPA_INTERFACE,eventReceiveHandler,subscribeAsyncHandler,"parodusInterface",10*20);
        if(rc != RBUS_ERROR_SUCCESS)
	{
		ParodusError("%s subscribe failed : %d - %s\n", WEBPA_INTERFACE, rc, rbusError_ToString(rc));
        }   
	return rc;	
} 
#endif

void processWebconfigUpstreamEvent(rbusHandle_t handle, rbusEvent_t const* event, rbusEventSubscription_t* subscription)
{
    (void)handle;
    (void)subscription;
    
    int rv=-1;
	wrp_msg_t *event_msg;
	void *bytes;
	const uint8_t* bytesVal = NULL;
    int len;
    rbusValue_t value = NULL;

    value = rbusObject_GetValue(event->data, "value");
    bytesVal = rbusValue_GetBytes(value, &len);

	bytes = (void*) bytesVal;
	rv = wrp_to_struct( bytes, len, WRP_BYTES, &event_msg );
	if(rv > 0)
	{
		ParodusInfo(" Received upstream event data: dest '%s'\n", event_msg->u.event.dest);
		partners_t *partnersList = NULL;
		int j = 0;

		int ret = validate_partner_id(event_msg, &partnersList);
		if(ret == 1)
		{
		    wrp_msg_t *eventMsg = (wrp_msg_t *) malloc(sizeof(wrp_msg_t));
		    memset( eventMsg, 0, sizeof( wrp_msg_t ) );
		    eventMsg->msg_type = event_msg->msg_type;
		    eventMsg->u.event.content_type=event_msg->u.event.content_type;
		    eventMsg->u.event.source=event_msg->u.event.source;
		    eventMsg->u.event.dest=event_msg->u.event.dest;
		    eventMsg->u.event.payload=event_msg->u.event.payload;
		    eventMsg->u.event.payload_size=event_msg->u.event.payload_size;
		    eventMsg->u.event.headers=event_msg->u.event.headers;
		    eventMsg->u.event.metadata=event_msg->u.event.metadata;
		    eventMsg->u.event.partner_ids = partnersList;
		    if(event_msg->u.event.transaction_uuid)
		    {
			  ParodusPrint("Inside Trans id in PARODUS_rbus\n");
		    }
		    else
		    {
			  ParodusPrint("Assigning NULL to trans id RBUS\n");
			  eventMsg->u.event.transaction_uuid = NULL;
		    }

		   int size = wrp_struct_to( eventMsg, WRP_BYTES, &bytes );
		   if(size > 0)
		   {
		       sendUpstreamMsgToServer(&bytes, size);
		   }
		   free(eventMsg);
		   free(bytes);
		   bytes = NULL;
		}
		else
		{
		   sendUpstreamMsgToServer((void **)(&bytes), len);
		}
		if(partnersList != NULL)
		{
		    for(j=0; j<(int)partnersList->count; j++)
		    {
		        if(NULL != partnersList->partner_ids[j])
		        {
		             free(partnersList->partner_ids[j]);
		        }
		    }
		    free(partnersList);
		}
		partnersList = NULL;
	}
}

void subscribeAsyncHandler( rbusHandle_t handle, rbusEventSubscription_t* subscription, rbusError_t error)
{
	(void)handle;
	ParodusInfo("subscribeAsyncHandler event %s, error %d - %s\n",subscription->eventName, error, rbusError_ToString(error));
}

#ifdef WAN_FAILOVER_SUPPORTED
void eventReceiveHandler( rbusHandle_t rbus_Handle, rbusEvent_t const* event, rbusEventSubscription_t* subscription )
{
    (void)subscription;
    ParodusPrint("Handling event inside eventReceiveHandler\n");
    (void)rbus_Handle;
    char * interface = NULL;
    rbusValue_t newValue = rbusObject_GetValue(event->data, "value");
    rbusValue_t oldValue = rbusObject_GetValue(event->data, "oldValue");
    ParodusInfo("Consumer received ValueChange event for param %s\n", event->name);
    
    if(newValue) {    
        interface = (char *) rbusValue_GetString(newValue, NULL);
	setWebpaInterface(interface);
    }
    else {
	   ParodusError("newValue is NULL\n");
    }

    if(newValue !=NULL && oldValue!=NULL && interface!=NULL) {
	ParodusInfo("New Value: %s Old Value: %s New Interface Value: %s\n", rbusValue_GetString(newValue, NULL), rbusValue_GetString(oldValue, NULL), interface);
	
	// If interface is already down then reset it and reconnect cloud conn as wan failover event is received
	if(get_interface_down_event()) 
	{
		reset_interface_down_event();
		ParodusInfo("Interface_down_event is reset\n");
		resume_heartBeatTimer();
	}
	// Close cloud conn and reconnect with the new interface as wan failover event is received
	set_global_reconnect_reason("WAN_FAILOVER");
	set_global_reconnect_status(true);
	set_close_retry();

    }
    else {
         if(oldValue == NULL) {
     	        ParodusError("oldValue is NULL\n");
         }  
         if(interface == NULL) {
	        ParodusError("interface is NULL\n"); 
         }  
    }
}
#endif