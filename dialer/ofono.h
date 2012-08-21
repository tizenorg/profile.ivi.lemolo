#ifndef _EFL_OFONO_H__
#define _EFL_OFONO_H__

typedef enum
{
	OFONO_API_SIM =		(1 <<  0),
	OFONO_API_NETREG =	(1 <<  1),
	OFONO_API_VOICE =	(1 <<  2),
	OFONO_API_MSG =		(1 <<  3),
	OFONO_API_MSG_WAITING =	(1 <<  4),
	OFONO_API_SMART_MSG = 	(1 <<  5),
	OFONO_API_STK =		(1 <<  6),
	OFONO_API_CALL_FW =	(1 <<  7),
	OFONO_API_CALL_VOL =	(1 <<  8),
	OFONO_API_CALL_METER =	(1 <<  9),
	OFONO_API_CALL_SET =	(1 << 10),
	OFONO_API_CALL_BAR =	(1 << 11),
	OFONO_API_SUPPL_SERV =	(1 << 12),
	OFONO_API_TXT_TEL = 	(1 << 13),
	OFONO_API_CELL_BROAD = 	(1 << 14),
	OFONO_API_CONNMAN =	(1 << 15),
	OFONO_API_PUSH_NOTIF =	(1 << 16),
	OFONO_API_PHONEBOOK = 	(1 << 17),
	OFONO_API_ASN =		(1 << 18)
} OFono_API;

typedef enum
{
	OFONO_ERROR_NONE = 0,
	OFONO_ERROR_FAILED,
	OFONO_ERROR_DOES_NOT_EXIST,
	OFONO_ERROR_IN_PROGRESS,
	OFONO_ERROR_IN_USE,
	OFONO_ERROR_INVALID_ARGS,
	OFONO_ERROR_INVALID_FORMAT,
	OFONO_ERROR_ACCESS_DENIED,
	OFONO_ERROR_ATTACH_IN_PROGRESS,
	OFONO_ERROR_INCORRECT_PASSWORD,
	OFONO_ERROR_NOT_ACTIVE,
	OFONO_ERROR_NOT_ALLOWED,
	OFONO_ERROR_NOT_ATTACHED,
	OFONO_ERROR_NOT_AVAILABLE,
	OFONO_ERROR_NOT_FOUND,
	OFONO_ERROR_NOT_IMPLEMENTED,
	OFONO_ERROR_NOT_RECOGNIZED,
	OFONO_ERROR_NOT_REGISTERED,
	OFONO_ERROR_NOT_SUPPORTED,
	OFONO_ERROR_SIM_NOT_READY,
	OFONO_ERROR_STK,
	OFONO_ERROR_TIMEDOUT,
	OFONO_ERROR_OFFLINE
} OFono_Error;

typedef enum
{
	OFONO_CALL_STATE_DISCONNECTED = 0,
	OFONO_CALL_STATE_ACTIVE,
	OFONO_CALL_STATE_HELD,
	OFONO_CALL_STATE_DIALING,
	OFONO_CALL_STATE_ALERTING,
	OFONO_CALL_STATE_INCOMING,
	OFONO_CALL_STATE_WAITING
} OFono_Call_State;

typedef enum
{
	OFONO_USSD_STATE_IDLE = 0,
	OFONO_USSD_STATE_ACTIVE,
	OFONO_USSD_STATE_USER_RESPONSE
} OFono_USSD_State;

typedef struct _OFono_Call OFono_Call;
typedef struct _OFono_Pending OFono_Pending;

typedef struct _OFono_Callback_List_USSD_Notify_Node OFono_Callback_List_USSD_Notify_Node;

typedef struct _OFono_Callback_List_Modem_Node OFono_Callback_List_Modem_Node;
typedef struct _OFono_Callback_List_Call_Node OFono_Callback_List_Call_Node;
typedef struct _OFono_Callback_List_Call_Disconnected_Node OFono_Callback_List_Call_Disconnected_Node;

typedef void (*OFono_Simple_Cb)(void *data, OFono_Error error);
typedef void (*OFono_String_Cb)(void *data, OFono_Error error, const char *str);
typedef void (*OFono_Call_Cb)(void *data, OFono_Error error, OFono_Call *call);


/* Voice Call: */
OFono_Pending *ofono_call_hangup(OFono_Call *c, OFono_Simple_Cb cb,
					const void *data);
OFono_Pending *ofono_call_answer(OFono_Call *c, OFono_Simple_Cb cb,
					const void *data);

OFono_Call_State ofono_call_state_get(const OFono_Call *c);
const char *ofono_call_name_get(const OFono_Call *c);
const char *ofono_call_line_id_get(const OFono_Call *c);
Eina_Bool ofono_call_multiparty_get(const OFono_Call *c);
double ofono_call_start_time_get(const OFono_Call *c);
time_t ofono_call_full_start_time_get(const OFono_Call *c);

#define ofono_call_state_valid_check(c) \
	(ofono_call_state_get(c) != OFONO_CALL_STATE_DISCONNECTED)

OFono_Callback_List_Call_Node *ofono_call_added_cb_add(
	void (*cb)(void *data,OFono_Call *call), const void *data);

OFono_Callback_List_Call_Node *ofono_call_removed_cb_add(
	void (*cb)(void *data, OFono_Call *call), const void *data);

OFono_Callback_List_Call_Node *ofono_call_changed_cb_add(
	void (*cb)(void *data, OFono_Call *call), const void *data);

OFono_Callback_List_Call_Disconnected_Node *ofono_call_disconnected_cb_add(
	void (*cb)(void *data, OFono_Call *call, const char *reason),
	const void *data);

OFono_Callback_List_USSD_Notify_Node *ofono_ussd_notify_cb_add(
	void (*cb)(void *data, Eina_Bool needs_reply, const char *msg),
	const void *data);

OFono_Pending *ofono_tones_send(const char *tones, OFono_Simple_Cb cb,
				const void *data);

void ofono_call_changed_cb_del(OFono_Callback_List_Call_Node *callback_node);
void ofono_call_disconnected_cb_del(OFono_Callback_List_Call_Disconnected_Node *callback_node);
void ofono_ussd_notify_cb_del(OFono_Callback_List_USSD_Notify_Node *callback_node);


void ofono_call_added_cb_del(OFono_Callback_List_Call_Node *callback_node);
void ofono_call_removed_cb_del(OFono_Callback_List_Call_Node *callback_node);

OFono_Pending *ofono_multiparty_create(OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_multiparty_hangup(OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_private_chat(OFono_Call *c, OFono_Simple_Cb cb,
					const void *data);

/* Modem: */
const char *ofono_modem_serial_get(void);

OFono_Pending *ofono_modem_change_pin(const char *what, const char *old, const char *new,
				OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_modem_reset_pin(const char *what, const char *puk, const char *new,
				OFono_Simple_Cb cb, const void *data);

OFono_Pending *ofono_dial(const char *number, const char *hide_callerid,
				OFono_Call_Cb cb, const void *data);

OFono_Pending *ofono_transfer(OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_swap_calls(OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_release_and_answer(OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_release_and_swap(OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_hold_and_answer(OFono_Simple_Cb cb, const void *data);
OFono_Pending *ofono_hangup_all(OFono_Simple_Cb cb, const void *data);

/* Call Volume: */

OFono_Pending *ofono_mute_set(Eina_Bool mute, OFono_Simple_Cb cb, const void *data);
Eina_Bool ofono_mute_get(void);

OFono_Pending *ofono_volume_speaker_set(unsigned char volume, OFono_Simple_Cb cb, const void *data);
unsigned char ofono_volume_speaker_get(void);


OFono_Pending *ofono_volume_microphone_set(unsigned char volume, OFono_Simple_Cb cb, const void *data);
unsigned char ofono_volume_microphone_get(void);

/* Voicemail (Message Waiting) */
Eina_Bool ofono_voicemail_waiting_get(void);
unsigned char ofono_voicemail_count_get(void);
const char *ofono_voicemail_number_get(void);

/* Supplementary Services */
OFono_Pending *ofono_ss_initiate(const char *command, OFono_String_Cb cb, const void *data);

OFono_Pending *ofono_ussd_respond(const char *string, OFono_String_Cb cb, const void *data);
OFono_Pending *ofono_ussd_cancel(OFono_Simple_Cb cb, const void *data);
OFono_USSD_State ofono_ussd_state_get(void);

/* Setup: */
void ofono_modem_api_list(FILE *fp, const char *prefix, const char *suffix);
void ofono_modem_api_require(const char *spec);
void ofono_modem_type_list(FILE *fp, const char *prefix, const char *suffix);
void ofono_modem_type_require(const char *spec);
void ofono_modem_path_wanted_set(const char *path);

unsigned int ofono_modem_api_get(void);

OFono_Callback_List_Modem_Node *ofono_modem_conected_cb_add(void (*cb)(void *data),
							const void *data);

OFono_Callback_List_Modem_Node *ofono_modem_disconnected_cb_add(
	void (*cb)(void *data), const void *data);

OFono_Callback_List_Modem_Node *ofono_modem_changed_cb_add(void (*cb)(void *data),
							const void *data);

void ofono_modem_changed_cb_del(OFono_Callback_List_Modem_Node *callback_node);
void ofono_modem_disconnected_cb_del(OFono_Callback_List_Modem_Node *callback_node);
void ofono_modem_connected_cb_del(OFono_Callback_List_Modem_Node *callback_node);

void ofono_pending_cancel(OFono_Pending *pending);

const char *ofono_error_message_get(OFono_Error e);

Eina_Bool ofono_init(void);
void ofono_shutdown(void);

#endif
