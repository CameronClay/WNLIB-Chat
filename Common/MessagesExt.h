#pragma once
#define MSG_CHANGE_CONNECTINIT -126
#define MSG_CHANGE_CONNECT -125

#define TYPE_REQUEST -126
#define MSG_REQUEST_AUTHENTICATION -128
#define MSG_REQUEST_TRANSFER -127
#define MSG_REQUEST_WHITEBOARD -126


#define TYPE_RESPONSE -125
#define MSG_RESPONSE_AUTH_DECLINED -128
#define MSG_RESPONSE_AUTH_CONFIRMED -127
#define MSG_RESPONSE_TRANSFER_DECLINED -126
#define MSG_RESPONSE_TRANSFER_CONFIRMED -125
#define MSG_RESPONSE_WHITEBOARD_CONFIRMED -124
#define MSG_RESPONSE_WHITEBOARD_DECLINED -123
#define MSG_RESPONSE_WHITEBOARD_INITED  -122

#define TYPE_FILE -124
#define MSG_FILE_LIST -128
#define MSG_FILE_DATA -127
#define MSG_FILE_SEND_CANCELED -126
#define MSG_FILE_RECEIVE_CANCELED -125

#define TYPE_ADMIN -123
#define MSG_ADMIN_NOT -128
#define MSG_ADMIN_KICK -127
#define MSG_ADMIN_CANNOTKICK -126

#define TYPE_DATA 127
#define MSG_DATA_TEXT 127
#define MSG_DATA_BITMAP 126
#define MSG_DATA_MOUSE 125

#define TYPE_VERSION 126
#define MSG_VERSION_CHECK 126
#define MSG_VERSION_UPTODATE 125
#define MSG_VERSION_OUTOFDATE 124

#define TYPE_WHITEBOARD 125
#define MSG_WHITEBOARD_ACTIVATE -127
#define MSG_WHITEBOARD_TERMINATE -126
#define MSG_WHITEBOARD_SETTINGS -125
#define MSG_WHITEBOARD_KICK -123
#define MSG_WHITEBOARD_LEFT -122
#define MSG_WHITEBOARD_CANNOTCREATE -121
#define MSG_WHITEBOARD_CANNOTTERMINATE -120

#define TYPE_TOOL 124
#define MSG_TOOL_CHANGE -127