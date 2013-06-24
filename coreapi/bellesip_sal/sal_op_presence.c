/*
linphone
Copyright (C) 2012  Belledonne Communications, Grenoble, France

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#include "sal_impl.h"


void sal_add_presence_info(SalOp *op, belle_sip_message_t *notify, SalPresenceModel *presence) {
	char *contact_info;
	char *content = NULL;
	size_t content_length;

	belle_sip_header_from_t *from=belle_sip_message_get_header_by_type(notify,belle_sip_header_from_t);

	contact_info=belle_sip_uri_to_string(belle_sip_header_address_get_uri(BELLE_SIP_HEADER_ADDRESS(from)));
	op->base.root->callbacks.convert_presence_to_xml_requested(op, presence, contact_info, &content);
	if (content == NULL) {
		ms_free(contact_info);
		return;
	}

	belle_sip_message_remove_header(BELLE_SIP_MESSAGE(notify),BELLE_SIP_CONTENT_TYPE);
	belle_sip_message_add_header(BELLE_SIP_MESSAGE(notify)
								,BELLE_SIP_HEADER(belle_sip_header_content_type_create("application","pidf+xml")));
	belle_sip_message_remove_header(BELLE_SIP_MESSAGE(notify),BELLE_SIP_CONTENT_LENGTH);
	belle_sip_message_add_header(BELLE_SIP_MESSAGE(notify)
								,BELLE_SIP_HEADER(belle_sip_header_content_length_create(content_length=strlen(content))));
	belle_sip_message_set_body(BELLE_SIP_MESSAGE(notify),content,content_length);
	ms_free(contact_info);
	ms_free(content);
}

static void presence_process_io_error(void *user_ctx, const belle_sip_io_error_event_t *event){
	ms_error("presence_process_io_error not implemented yet");
}
static void presence_process_dialog_terminated(void *ctx, const belle_sip_dialog_terminated_event_t *event) {
	SalOp* op= (SalOp*)ctx;
	if (op->dialog) {
		sal_op_unref(op);
		op->dialog=NULL;
	}
}

static void presence_response_event(void *op_base, const belle_sip_response_event_t *event){
	SalOp* op = (SalOp*)op_base;
	belle_sip_dialog_state_t dialog_state;
	belle_sip_client_transaction_t* client_transaction = belle_sip_response_event_get_client_transaction(event);
	belle_sip_response_t* response=belle_sip_response_event_get_response(event);
	belle_sip_request_t* request=belle_sip_transaction_get_request(BELLE_SIP_TRANSACTION(client_transaction));
	int code = belle_sip_response_get_status_code(response);
	char reason[256]={0};
	SalError error=SalErrorUnknown;
	SalReason sr=SalReasonUnknown;
	belle_sip_header_expires_t* expires;

	if (sal_compute_sal_errors(response,&error,&sr,reason, sizeof(reason))) {
		ms_error("subscription to [%s] rejected reason [%s]",sal_op_get_to(op),reason[0]!=0?reason:sal_reason_to_string(sr));
		op->base.root->callbacks.notify_presence(op,SalSubscribeTerminated, SalPresenceOffline,NULL);
		return;
	}
	set_or_update_dialog(op_base,belle_sip_response_event_get_dialog(event));
	if (!op->dialog) {
		ms_message("presence op [%p] receive out of dialog answer [%i]",op,code);
		return;
	}
	dialog_state=belle_sip_dialog_get_state(op->dialog);

	switch(dialog_state) {
		case BELLE_SIP_DIALOG_NULL:
		case BELLE_SIP_DIALOG_EARLY: {
			ms_error("presence op [%p] receive an unexpected answer [%i]",op,code);
			break;
		}
		case BELLE_SIP_DIALOG_CONFIRMED: {
			if (strcmp("SUBSCRIBE",belle_sip_request_get_method(request))==0) {
				expires=belle_sip_message_get_header_by_type(request,belle_sip_header_expires_t);
				if(op->refresher) {
					belle_sip_refresher_stop(op->refresher);
					belle_sip_object_unref(op->refresher);
					op->refresher=NULL;
				}
				if (expires>0){
					op->refresher=belle_sip_client_transaction_create_refresher(client_transaction);
				}
			}
			break;
		}
		case BELLE_SIP_DIALOG_TERMINATED:
			if (op->refresher) {
				belle_sip_refresher_stop(op->refresher);
				belle_sip_object_unref(op->refresher);
				op->refresher=NULL;
			}
		break;
		default: {
			ms_error("presence op [%p] receive answer [%i] not implemented",op,code);
		}
		/* no break */
	}


}
static void presence_process_timeout(void *user_ctx, const belle_sip_timeout_event_t *event) {
	ms_error("presence_process_timeout not implemented yet");
}

static void presence_process_transaction_terminated(void *user_ctx, const belle_sip_transaction_terminated_event_t *event) {
	ms_message("presence_process_transaction_terminated not implemented yet");
}

static SalPresenceModel * process_presence_notification(SalOp *op, belle_sip_request_t *req) {
	belle_sip_header_content_type_t *content_type = belle_sip_message_get_header_by_type(BELLE_SIP_MESSAGE(req), belle_sip_header_content_type_t);
	belle_sip_header_content_length_t *content_length = belle_sip_message_get_header_by_type(BELLE_SIP_MESSAGE(req), belle_sip_header_content_length_t);
	const char *body = belle_sip_message_get_body(BELLE_SIP_MESSAGE(req));
	SalPresenceModel *result = NULL;

	if ((content_type == NULL) || (content_length == NULL))
		return NULL;
	if (belle_sip_header_content_length_get_content_length(content_length) == 0)
		return NULL;

	op->base.root->callbacks.parse_presence_requested(op,
							  belle_sip_header_content_type_get_type(content_type),
							  belle_sip_header_content_type_get_subtype(content_type),
							  body,
							  &result);

	return result;
}

static void handle_notify(SalOp *op, belle_sip_request_t *req){
	belle_sip_response_t* resp;
	belle_sip_server_transaction_t* server_transaction=op->pending_server_trans;
	belle_sip_header_subscription_state_t* subscription_state_header=belle_sip_message_get_header_by_type(req,belle_sip_header_subscription_state_t);
	SalSubscribeStatus sub_state;
	
	if (strcmp("NOTIFY",belle_sip_request_get_method(req))==0) {
		SalPresenceModel *presence_model = NULL;
		const char* body = belle_sip_message_get_body(BELLE_SIP_MESSAGE(req));
		if (body==NULL){
			ms_error("No body in NOTIFY received from [%s]",sal_op_get_from(op));
			resp = sal_op_create_response_from_request(op, req, 415);
			belle_sip_server_transaction_send_response(server_transaction,resp);
			return;
		}
		presence_model = process_presence_notification(op, req);
		if (presence_model != NULL) {
			/* Presence notification body parsed successfully. */
			if (!subscription_state_header || strcasecmp(BELLE_SIP_SUBSCRIPTION_STATE_TERMINATED,belle_sip_header_subscription_state_get_state(subscription_state_header)) ==0) {
				sub_state=SalSubscribeTerminated;
				ms_message("Outgoing subscription terminated by remote [%s]",sal_op_get_to(op));
			} else {
				sub_state=SalSubscribeActive;
			}
			op->base.root->callbacks.notify_presence(op, sub_state, presence_model, NULL);
			resp = sal_op_create_response_from_request(op, req, 200);
		} else {
			/* Formatting error in presence notification body. */
			ms_error("Wrongly formatted presence notification received");
			resp = sal_op_create_response_from_request(op, req, 400);
		}
		belle_sip_server_transaction_send_response(server_transaction,resp);
	}
}

static void presence_process_request_event(void *op_base, const belle_sip_request_event_t *event) {
	SalOp* op = (SalOp*)op_base;
	belle_sip_server_transaction_t* server_transaction = belle_sip_provider_create_server_transaction(op->base.root->prov,belle_sip_request_event_get_request(event));
	belle_sip_request_t* req = belle_sip_request_event_get_request(event);
	belle_sip_dialog_state_t dialog_state;
	belle_sip_header_expires_t* expires = belle_sip_message_get_header_by_type(req,belle_sip_header_expires_t);
	belle_sip_response_t* resp;
	const char *method=belle_sip_request_get_method(req);
	
	belle_sip_object_ref(server_transaction);
	if (op->pending_server_trans)  belle_sip_object_unref(op->pending_server_trans);
	op->pending_server_trans=server_transaction;


	if (!op->dialog) {
		if (strcmp(method,"SUBSCRIBE")==0){
			op->dialog=belle_sip_provider_create_dialog(op->base.root->prov,BELLE_SIP_TRANSACTION(server_transaction));
			belle_sip_dialog_set_application_data(op->dialog,op);
			sal_op_ref(op);
			ms_message("new incoming subscription from [%s] to [%s]",sal_op_get_from(op),sal_op_get_to(op));
		}else{ /* this is a NOTIFY */
			ms_message("Receiving out of dialog notify");
			handle_notify(op,req);
			return;
		}
	}
	dialog_state=belle_sip_dialog_get_state(op->dialog);
	switch(dialog_state) {
		case BELLE_SIP_DIALOG_NULL: {
			op->base.root->callbacks.subscribe_presence_received(op,sal_op_get_from(op));
			break;
		}
		case BELLE_SIP_DIALOG_EARLY:
			ms_error("unexpected method [%s] for dialog [%p] in state BELLE_SIP_DIALOG_EARLY ",method,op->dialog);
			break;

		case BELLE_SIP_DIALOG_CONFIRMED:
			if (strcmp("NOTIFY",method)==0) {
				handle_notify(op,req);
			} else if (strcmp("SUBSCRIBE",method)==0) {
				/*either a refresh or an unsubscribe*/
				if (expires && belle_sip_header_expires_get_expires(expires)>0) {
					op->base.root->callbacks.subscribe_presence_received(op,sal_op_get_from(op));
				} else if(expires) {
					ms_message("Unsubscribe received from [%s]",sal_op_get_from(op));
					resp=sal_op_create_response_from_request(op,req,200);
					belle_sip_server_transaction_send_response(server_transaction,resp);
				}
			}
			break;
		default: 
			ms_error("unexpected dialog state [%s]",belle_sip_dialog_state_to_string(dialog_state));
			break;
	}
}

void sal_op_presence_fill_cbs(SalOp*op) {
	op->callbacks.process_io_error=presence_process_io_error;
	op->callbacks.process_response_event=presence_response_event;
	op->callbacks.process_timeout=presence_process_timeout;
	op->callbacks.process_transaction_terminated=presence_process_transaction_terminated;
	op->callbacks.process_request_event=presence_process_request_event;
	op->callbacks.process_dialog_terminated=presence_process_dialog_terminated;
	op->type=SalOpPresence;
}


/*presence Subscribe/notify*/
int sal_subscribe_presence(SalOp *op, const char *from, const char *to){
	belle_sip_request_t *req=NULL;
	if (from)
		sal_op_set_from(op,from);
	if (to)
		sal_op_set_to(op,to);

	sal_op_presence_fill_cbs(op);

	/*???sal_exosip_fix_route(op); make sure to ha ;lr*/
	req=sal_op_build_request(op,"SUBSCRIBE");
	belle_sip_message_add_header(BELLE_SIP_MESSAGE(req),belle_sip_header_create("Event","presence"));
	belle_sip_message_add_header(BELLE_SIP_MESSAGE(req),BELLE_SIP_HEADER(belle_sip_header_expires_create(600)));

	return sal_op_send_request(op,req);
}


static belle_sip_request_t *create_presence_notify(SalOp *op){
	belle_sip_request_t* notify=belle_sip_dialog_create_request(op->dialog,"NOTIFY");
	belle_sip_message_add_header((belle_sip_message_t*)notify,belle_sip_header_create("Event","presence"));
	return notify;
}

int sal_notify_presence(SalOp *op, SalPresenceModel *presence){
	belle_sip_request_t* notify=create_presence_notify(op);
	sal_add_presence_info(op,BELLE_SIP_MESSAGE(notify),presence); /*FIXME, what about expires ??*/
	belle_sip_message_add_header(BELLE_SIP_MESSAGE(notify)
			,BELLE_SIP_HEADER(belle_sip_header_subscription_state_create(BELLE_SIP_SUBSCRIPTION_STATE_ACTIVE,600)));
	return sal_op_send_request(op,notify);
}

int sal_notify_presence_close(SalOp *op){
	belle_sip_request_t* notify=create_presence_notify(op);
	sal_add_presence_info(op,BELLE_SIP_MESSAGE(notify),NULL); /*FIXME, what about expires ??*/
	belle_sip_message_add_header(BELLE_SIP_MESSAGE(notify)
		,BELLE_SIP_HEADER(belle_sip_header_subscription_state_create(BELLE_SIP_SUBSCRIPTION_STATE_TERMINATED,-1)));
	return sal_op_send_request(op,notify);
}



