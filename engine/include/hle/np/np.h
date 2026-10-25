#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>

namespace gravity::hle::np {

// Common Return Codes
inline constexpr std::int32_t SCE_OK = 0;
inline constexpr std::int32_t SCE_COMMON_DIALOG_STATUS_NONE = 0;
inline constexpr std::int32_t SCE_COMMON_DIALOG_STATUS_RUNNING = 2;
inline constexpr std::int32_t SCE_COMMON_DIALOG_STATUS_FINISHED = 3;

// SceNp Title Structures
struct SceNpTitleId {
    char id[10];
    char padding[6];
};

struct SceNpTitleSecret {
    std::uint8_t secret[128];
};

struct SceNpId {
    std::uint64_t account_id;
    char handle[16];
    std::uint8_t reserved[8];
};

// libSceNpManager
extern "C" {
GRAVITY_SYSV_ABI std::int32_t sceNpSetNpTitleId(const SceNpTitleId* title_id, const SceNpTitleSecret* title_secret);
GRAVITY_SYSV_ABI std::int32_t sceNpSetContentRestriction(const void* restriction);
GRAVITY_SYSV_ABI std::int32_t sceNpGetOnlineId(std::int32_t user_id, char* online_id);
GRAVITY_SYSV_ABI std::int32_t sceNpGetNpId(std::int32_t user_id, SceNpId* np_id);
GRAVITY_SYSV_ABI std::int32_t sceNpGetState(std::int32_t user_id, std::int32_t* state);
GRAVITY_SYSV_ABI std::int32_t sceNpCheckCallback();
GRAVITY_SYSV_ABI std::int32_t sceNpCheckNpAvailability(std::int32_t user_id);
GRAVITY_SYSV_ABI std::int32_t sceNpCreateRequest(std::int32_t ctx_id);
GRAVITY_SYSV_ABI std::int32_t sceNpDeleteRequest(std::int32_t req_id);
GRAVITY_SYSV_ABI std::int32_t sceNpRegisterStateCallback(void* callback, void* context);
GRAVITY_SYSV_ABI std::int32_t sceNpUnregisterStateCallback(std::int32_t cb_id);
GRAVITY_SYSV_ABI std::int32_t sceNpPollAsync(std::int32_t req_id, void* result);
GRAVITY_SYSV_ABI std::int32_t sceNpWaitAsync(std::int32_t req_id, void* result);
GRAVITY_SYSV_ABI std::int32_t sceNpManagerGetParentalControlInfo(void* info);
GRAVITY_SYSV_ABI std::int32_t sceNpGetNpTitleCtx();
GRAVITY_SYSV_ABI std::int32_t sceNpRegisterNpTitleCtx(void* ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpUnregisterNpTitleCtx(void* ctx);

// libSceCommonDialog
GRAVITY_SYSV_ABI std::int32_t sceCommonDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceCommonDialogIsUsed();

// libSceMsgDialog
GRAVITY_SYSV_ABI std::int32_t sceMsgDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceMsgDialogOpen(const void* param);
GRAVITY_SYSV_ABI std::int32_t sceMsgDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceMsgDialogGetResult(void* result);
GRAVITY_SYSV_ABI std::int32_t sceMsgDialogTerminate();

// libSceErrorDialog
GRAVITY_SYSV_ABI std::int32_t sceErrorDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceErrorDialogOpen(const void* param);
GRAVITY_SYSV_ABI std::int32_t sceErrorDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceErrorDialogTerminate();

// libSceNpTrophy
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyCreateContext(std::int32_t* ctx, void* id, void* secret, std::uint64_t opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyCreateHandle(std::int32_t* handle);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyDestroyHandle(std::int32_t handle);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyDestroyContext(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyRegisterContext(std::int32_t ctx, std::int32_t handle, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyAbortHandle(std::int32_t handle);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyUnlockTrophy(std::int32_t ctx, std::int32_t handle, std::int32_t trophy_id, std::uint64_t opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGameInfo(std::int32_t ctx, std::int32_t handle, void* details, void* data);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetTrophyInfo(std::int32_t ctx, std::int32_t handle, std::int32_t trophy_id, void* details, void* data);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGroupInfo(std::int32_t ctx, std::int32_t handle, std::int32_t group_id, void* details, void* data);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGameIcon(std::int32_t ctx, std::int32_t handle, void* icon, std::size_t* size);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGroupIcon(std::int32_t ctx, std::int32_t handle, std::int32_t group_id, void* icon, std::size_t* size);
GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetTrophyIcon(std::int32_t ctx, std::int32_t handle, std::int32_t trophy_id, void* icon, std::size_t* size);

// libSceNpCommon
GRAVITY_SYSV_ABI std::int32_t sceNpCmpNpId(const void* id1, const void* id2);

// libSceNet
GRAVITY_SYSV_ABI std::int32_t sceNetInit();
GRAVITY_SYSV_ABI std::int32_t sceNetTerm();
GRAVITY_SYSV_ABI std::int32_t sceNetPoolCreate(const char* name, std::int32_t size, std::int32_t flags);
GRAVITY_SYSV_ABI std::int32_t sceNetPoolDestroy(std::int32_t pool_id);
GRAVITY_SYSV_ABI std::int32_t* sceNetErrnoLoc();
GRAVITY_SYSV_ABI std::uint16_t sceNetHtons(std::uint16_t n);
GRAVITY_SYSV_ABI std::uint16_t sceNetNtohs(std::uint16_t n);
GRAVITY_SYSV_ABI std::int32_t sceNetInetPton(std::int32_t af, const char* src, void* dst);
GRAVITY_SYSV_ABI std::int32_t sceNetSocket(const char* name, std::int32_t domain, std::int32_t type, std::int32_t protocol);
GRAVITY_SYSV_ABI std::int32_t sceNetSocketClose(std::int32_t s);
GRAVITY_SYSV_ABI std::int32_t sceNetConnect(std::int32_t s, const void* name, std::uint32_t namelen);
GRAVITY_SYSV_ABI std::int32_t sceNetSend(std::int32_t s, const void* msg, std::size_t len, std::int32_t flags);
GRAVITY_SYSV_ABI std::int32_t sceNetRecv(std::int32_t s, void* buf, std::size_t len, std::int32_t flags);
GRAVITY_SYSV_ABI std::int32_t sceNetBind(std::int32_t s, const void* addr, std::uint32_t addrlen);
GRAVITY_SYSV_ABI std::int32_t sceNetListen(std::int32_t s, std::int32_t backlog);
GRAVITY_SYSV_ABI std::int32_t sceNetAccept(std::int32_t s, void* addr, std::uint32_t* addrlen);
GRAVITY_SYSV_ABI std::int32_t sceNetSetsockopt(std::int32_t s, std::int32_t level, std::int32_t optname, const void* optval, std::uint32_t optlen);
GRAVITY_SYSV_ABI std::int32_t sceNetEpollCreate(const char* name, std::int32_t flags);
GRAVITY_SYSV_ABI std::int32_t sceNetEpollControl(std::int32_t epfd, std::int32_t op, std::int32_t fd, void* event);
GRAVITY_SYSV_ABI std::int32_t sceNetEpollWait(std::int32_t epfd, void* events, std::int32_t maxevents, std::int32_t timeout);
GRAVITY_SYSV_ABI std::int32_t sceNetEpollDestroy(std::int32_t epfd);

// libSceNetCtl
GRAVITY_SYSV_ABI std::int32_t sceNetCtlInit();
GRAVITY_SYSV_ABI std::int32_t sceNetCtlTerm();
GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetState(std::int32_t* state);
GRAVITY_SYSV_ABI std::int32_t sceNetCtlRegisterCallback(void* cb, void* arg, std::int32_t* cb_id);
GRAVITY_SYSV_ABI std::int32_t sceNetCtlUnregisterCallback(std::int32_t cb_id);
GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetInfo(std::int32_t code, void* info);
GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetNatInfo(void* nat_info);
GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetResult(std::int32_t code, std::int32_t* result);
GRAVITY_SYSV_ABI std::int32_t sceNetCtlCheckCallback();

// libSceSsl
GRAVITY_SYSV_ABI std::int32_t sceSslInit(std::size_t pool_size);
GRAVITY_SYSV_ABI std::int32_t sceSslTerm();

// libSceHttp
GRAVITY_SYSV_ABI std::int32_t sceHttpInit(std::size_t pool_size);
GRAVITY_SYSV_ABI std::int32_t sceHttpTerm();
GRAVITY_SYSV_ABI std::int32_t sceHttpCreateConnectionWithURL(std::int32_t tmpl_id, const char* url, std::int32_t is_keepalive);
GRAVITY_SYSV_ABI std::int32_t sceHttpDeleteConnection(std::int32_t conn_id);
GRAVITY_SYSV_ABI std::int32_t sceHttpCreateRequestWithURL(std::int32_t conn_id, std::int32_t method, const char* path, std::uint64_t content_len);
GRAVITY_SYSV_ABI std::int32_t sceHttpDeleteRequest(std::int32_t req_id);
GRAVITY_SYSV_ABI std::int32_t sceHttpSendRequest(std::int32_t req_id, const void* post_data, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceHttpCreateTemplate(const char* user_agent, std::int32_t http_ver, std::int32_t auto_redirect);
GRAVITY_SYSV_ABI std::int32_t sceHttpDeleteTemplate(std::int32_t tmpl_id);
GRAVITY_SYSV_ABI std::int32_t sceHttpGetStatusCode(std::int32_t req_id, std::int32_t* status_code);
GRAVITY_SYSV_ABI std::int32_t sceHttpGetResponseContentLength(std::int32_t req_id, std::int32_t* is_chunked, std::uint64_t* content_length);
GRAVITY_SYSV_ABI std::int32_t sceHttpReadData(std::int32_t req_id, void* buf, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceHttpAddRequestHeader(std::int32_t req_id, const char* name, const char* value, std::int32_t mode);
GRAVITY_SYSV_ABI std::int32_t sceHttpUriEscape(char* out, std::size_t* out_len, const char* in, std::size_t in_len);

// libSceNpScore
GRAVITY_SYSV_ABI std::int32_t sceNpScoreCreateNpTitleCtx(void* ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreDeleteNpTitleCtx(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreCreateRequest(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreDeleteRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreRecordScore(std::int32_t req, std::int32_t board_id, std::int64_t score, const void* comment, std::size_t comment_size, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreRecordScoreAsync(std::int32_t req, std::int32_t board_id, std::int64_t score, const void* comment, std::size_t comment_size, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpScorePollAsync(std::int32_t req, void* result);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreAbortRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetBoardInfo(std::int32_t req, std::int32_t board_id, void* info);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByRange(std::int32_t req, std::int32_t board_id, std::uint32_t start, std::uint32_t count, void* ranking);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByRangeAsync(std::int32_t req, std::int32_t board_id, std::uint32_t start, std::uint32_t count, void* ranking);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByNpId(std::int32_t req, std::int32_t board_id, const void* np_ids, std::uint32_t count, void* ranking);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByNpIdAsync(std::int32_t req, std::int32_t board_id, const void* np_ids, std::uint32_t count, void* ranking);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetFriendsRanking(std::int32_t req, std::int32_t board_id, void* ranking);
GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetFriendsRankingAsync(std::int32_t req, std::int32_t board_id, void* ranking);

// libSceNpTus
GRAVITY_SYSV_ABI std::int32_t sceNpTusCreateNpTitleCtx(void* ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpTusDeleteNpTitleCtx(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpTusCreateRequest(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpTusDeleteRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpTusSetData(std::int32_t req, std::int32_t slot_id, const void* data, std::size_t size, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTusSetDataVUser(std::int32_t req, std::int32_t slot_id, const void* data, std::size_t size, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTusGetData(std::int32_t req, std::int32_t slot_id, void* data, std::size_t size, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTusGetDataVUser(std::int32_t req, std::int32_t slot_id, void* data, std::size_t size, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTusSetMultiSlotVariable(std::int32_t req, const void* slots, std::uint32_t count, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTusSetMultiSlotVariableVUser(std::int32_t req, const void* slots, std::uint32_t count, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTusGetMultiSlotVariable(std::int32_t req, const void* slots, std::uint32_t count, void* result, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTusGetMultiSlotVariableVUser(std::int32_t req, const void* slots, std::uint32_t count, void* result, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpTssGetData(std::int32_t req, std::int32_t slot_id, void* data, std::size_t size, const void* opt);

// libSceNpUtility
GRAVITY_SYSV_ABI std::int32_t sceNpLookupCreateTitleCtx(void* ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpLookupDeleteTitleCtx(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpLookupCreateRequest(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpLookupDeleteRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpLookupNpId(std::int32_t req, const void* np_id, void* result);
GRAVITY_SYSV_ABI std::int32_t sceNpBandwidthTestInitStart();
GRAVITY_SYSV_ABI std::int32_t sceNpBandwidthTestGetStatus(void* status);
GRAVITY_SYSV_ABI std::int32_t sceNpBandwidthTestShutdown();
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCreateTitleCtx(void* ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterDeleteTitleCtx(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCreateRequest(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCreateAsyncRequest(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterDeleteRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterAbortRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterPollAsync(std::int32_t req, void* result);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCensorComment(std::int32_t req, const char* src, char* dst, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterSanitizeComment(std::int32_t req, const char* src, char* dst, std::size_t size);

// libSceNpAuth
GRAVITY_SYSV_ABI std::int32_t sceNpAuthCreateRequest(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpAuthDeleteRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpAuthGetAuthorizationCode(std::int32_t req, const void* param, void* code);

// libSceNpCommerce
GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogOpen(const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogTerminate();
GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogGetResult(void* result);

// libSceNpMatching2
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2Initialize();
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2Terminate();
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetUserInfo(const void* info);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2ContextStart(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2ContextStop(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2DestroyContext(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2CreateContext(std::int32_t* ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterContextCallback(std::int32_t ctx, void* cb, void* arg);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterRoomEventCallback(std::int32_t ctx, void* cb, void* arg);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterRoomMessageCallback(std::int32_t ctx, void* cb, void* arg);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterSignalingCallback(std::int32_t ctx, void* cb, void* arg);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetDefaultRequestOptParam(const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetServerId(std::int32_t ctx, void* id);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2LeaveRoom(std::int32_t ctx, std::int64_t room_id);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2JoinRoom(std::int32_t ctx, std::int64_t room_id, const void* opt);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2CreateJoinRoom(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SearchRoom(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetUserInfoList(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetWorldInfoList(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SendRoomMessage(std::int32_t ctx, std::int64_t room_id, const void* msg, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SendRoomChatMessage(std::int32_t ctx, std::int64_t room_id, const void* msg, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetRoomDataExternalList(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetRoomDataInternal(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetRoomMemberDataInternal(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetRoomDataExternal(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetRoomDataInternal(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetRoomMemberDataInternal(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2KickoutRoomMember(std::int32_t ctx, std::int64_t room_id, std::uint64_t member_id);
GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SignalingGetConnectionInfo(std::int32_t ctx, void* info);

// Dialogs
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogOpen(const void* param);
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogGetResult(void* result);
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogTerminate();

GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogOpen(const void* param);
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogGetResult(void* result);
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogTerminate();

GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogOpen(const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogTerminate();

GRAVITY_SYSV_ABI std::int32_t sceNpSnsInitialize();
GRAVITY_SYSV_ABI std::int32_t sceNpSnsTerminate();
GRAVITY_SYSV_ABI std::int32_t sceNpSnsCheckCallback();

GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogInitialize();
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogOpen(const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogUpdateStatus();
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogGetResult(void* result);
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogTerminate();

GRAVITY_SYSV_ABI std::int32_t sceNpWebApiInitialize();
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateContext(void* ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteContext(std::int32_t ctx);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiTerminate();
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateRequest(std::int32_t ctx, const void* param);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiSendRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiAbortRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteRequest(std::int32_t req);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiGetHttpStatusCode(std::int32_t req, std::int32_t* code);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiReadData(std::int32_t req, void* buf, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateHandle(void* handle);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreatePushEventFilter(void* filter);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateServicePushEventFilter(void* filter);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteServicePushEventFilter(std::int32_t filter);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeletePushEventFilter(std::int32_t filter);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteHandle(std::int32_t handle);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiGetHttpResponseHeaderValueLength(std::int32_t req, const char* name, std::size_t* len);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiGetHttpResponseHeaderValue(std::int32_t req, const char* name, char* val, std::size_t len);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiRegisterPushEventCallback(void* cb, void* arg);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiRegisterServicePushEventCallback(void* cb, void* arg);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiUnregisterServicePushEventCallback(std::int32_t cb_id);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiUnregisterPushEventCallback(std::int32_t cb_id);
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiUtilityParseNpId(const char* str, void* np_id);
}

} // namespace gravity::hle::np
