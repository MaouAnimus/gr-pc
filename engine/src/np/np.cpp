#include "hle/np/np.h"
#include "hle/guest_memory.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <algorithm>

namespace gravity::hle::np {

namespace {
static thread_local std::int32_t s_net_errno = 0;
static std::mutex g_np_mutex;
static bool g_common_dialog_used = false;
}

extern "C" {

// =========================================================================
// libSceNpManager
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpSetNpTitleId(const SceNpTitleId* title_id, const SceNpTitleSecret* title_secret) {
    (void)title_secret;
    std::lock_guard<std::mutex> lock(g_np_mutex);
    if (title_id && guest_memory::accessible(title_id, sizeof(*title_id))) {
        char buf[16]{};
        std::memcpy(buf, title_id->id, (std::min)(sizeof(title_id->id), sizeof(buf) - 1));
        std::fprintf(stderr, "[NP] SetNpTitleId: %s (SCE_OK)\n", buf);
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpSetContentRestriction(const void* restriction) {
    (void)restriction;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpGetOnlineId(std::int32_t user_id, char* online_id) {
    (void)user_id;
    if (online_id && guest_memory::accessible(online_id, 16)) {
        constexpr const char name[] = "Kat";
        std::memcpy(online_id, name, sizeof(name));
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpGetNpId(std::int32_t user_id, SceNpId* np_id) {
    (void)user_id;
    if (np_id && guest_memory::accessible(np_id, sizeof(*np_id))) {
        std::memset(np_id, 0, sizeof(*np_id));
        np_id->account_id = 0x10001ULL;
        constexpr const char handle[] = "Kat";
        std::memcpy(np_id->handle, handle, sizeof(handle));
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpGetState(std::int32_t user_id, std::int32_t* state) {
    (void)user_id;
    if (state && guest_memory::accessible(state, sizeof(*state))) {
        *state = 2; // SCE_NP_STATE_SIGNED_IN
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpCheckCallback() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpCheckNpAvailability(std::int32_t user_id) {
    (void)user_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpCreateRequest(std::int32_t ctx_id) {
    (void)ctx_id;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpDeleteRequest(std::int32_t req_id) {
    (void)req_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpRegisterStateCallback(void* callback, void* context) {
    (void)callback; (void)context;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpUnregisterStateCallback(std::int32_t cb_id) {
    (void)cb_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpPollAsync(std::int32_t req_id, void* result) {
    (void)req_id; (void)result;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWaitAsync(std::int32_t req_id, void* result) {
    (void)req_id; (void)result;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpManagerGetParentalControlInfo(void* info) {
    (void)info;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpGetNpTitleCtx() {
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpRegisterNpTitleCtx(void* ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpUnregisterNpTitleCtx(void* ctx) {
    (void)ctx;
    return SCE_OK;
}

// =========================================================================
// libSceCommonDialog
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceCommonDialogInitialize() {
    std::fprintf(stderr, "[CommonDialog] Initialized (SCE_OK)\n");
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceCommonDialogIsUsed() {
    return g_common_dialog_used ? 1 : 0;
}

// =========================================================================
// libSceMsgDialog
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceMsgDialogInitialize() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceMsgDialogOpen(const void* param) {
    (void)param;
    g_common_dialog_used = true;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceMsgDialogUpdateStatus() {
    g_common_dialog_used = false;
    return SCE_COMMON_DIALOG_STATUS_FINISHED;
}

GRAVITY_SYSV_ABI std::int32_t sceMsgDialogGetResult(void* result) {
    if (result && guest_memory::accessible(result, 4)) {
        *reinterpret_cast<std::int32_t*>(result) = 0;
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceMsgDialogTerminate() {
    g_common_dialog_used = false;
    return SCE_OK;
}

// =========================================================================
// libSceErrorDialog
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceErrorDialogInitialize() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceErrorDialogOpen(const void* param) {
    (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceErrorDialogUpdateStatus() {
    return SCE_COMMON_DIALOG_STATUS_FINISHED;
}

GRAVITY_SYSV_ABI std::int32_t sceErrorDialogTerminate() {
    return SCE_OK;
}

// =========================================================================
// libSceNpTrophy
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyCreateContext(std::int32_t* ctx, void* id, void* secret, std::uint64_t opt) {
    (void)id; (void)secret; (void)opt;
    if (ctx && guest_memory::accessible(ctx, sizeof(*ctx))) {
        *ctx = 1;
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyCreateHandle(std::int32_t* handle) {
    if (handle && guest_memory::accessible(handle, sizeof(*handle))) {
        *handle = 1;
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyDestroyHandle(std::int32_t handle) {
    (void)handle;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyDestroyContext(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyRegisterContext(std::int32_t ctx, std::int32_t handle, const void* opt) {
    (void)ctx; (void)handle; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyAbortHandle(std::int32_t handle) {
    (void)handle;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyUnlockTrophy(std::int32_t ctx, std::int32_t handle, std::int32_t trophy_id, std::uint64_t opt) {
    (void)ctx; (void)handle; (void)trophy_id; (void)opt;
    std::fprintf(stderr, "[Trophy] Unlocked trophy id=%d\n", trophy_id);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGameInfo(std::int32_t ctx, std::int32_t handle, void* details, void* data) {
    (void)ctx; (void)handle; (void)details; (void)data;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetTrophyInfo(std::int32_t ctx, std::int32_t handle, std::int32_t trophy_id, void* details, void* data) {
    (void)ctx; (void)handle; (void)trophy_id; (void)details; (void)data;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGroupInfo(std::int32_t ctx, std::int32_t handle, std::int32_t group_id, void* details, void* data) {
    (void)ctx; (void)handle; (void)group_id; (void)details; (void)data;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGameIcon(std::int32_t ctx, std::int32_t handle, void* icon, std::size_t* size) {
    (void)ctx; (void)handle; (void)icon; (void)size;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetGroupIcon(std::int32_t ctx, std::int32_t handle, std::int32_t group_id, void* icon, std::size_t* size) {
    (void)ctx; (void)handle; (void)group_id; (void)icon; (void)size;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTrophyGetTrophyIcon(std::int32_t ctx, std::int32_t handle, std::int32_t trophy_id, void* icon, std::size_t* size) {
    (void)ctx; (void)handle; (void)trophy_id; (void)icon; (void)size;
    return SCE_OK;
}

// =========================================================================
// libSceNpCommon
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpCmpNpId(const void* id1, const void* id2) {
    if (!id1 || !id2) return -1;
    return std::memcmp(id1, id2, sizeof(SceNpId)) == 0 ? 0 : 1;
}

// =========================================================================
// libSceNet & libSceNetCtl
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNetInit() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetTerm() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetPoolCreate(const char* name, std::int32_t size, std::int32_t flags) {
    (void)name; (void)size; (void)flags;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNetPoolDestroy(std::int32_t pool_id) {
    (void)pool_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t* sceNetErrnoLoc() {
    return &s_net_errno;
}

GRAVITY_SYSV_ABI std::uint16_t sceNetHtons(std::uint16_t n) {
    return static_cast<std::uint16_t>((n >> 8) | (n << 8));
}

GRAVITY_SYSV_ABI std::uint16_t sceNetNtohs(std::uint16_t n) {
    return static_cast<std::uint16_t>((n >> 8) | (n << 8));
}

GRAVITY_SYSV_ABI std::int32_t sceNetInetPton(std::int32_t af, const char* src, void* dst) {
    (void)af; (void)src; (void)dst;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNetSocket(const char* name, std::int32_t domain, std::int32_t type, std::int32_t protocol) {
    (void)name; (void)domain; (void)type; (void)protocol;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNetSocketClose(std::int32_t s) {
    (void)s;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetConnect(std::int32_t s, const void* name, std::uint32_t namelen) {
    (void)s; (void)name; (void)namelen;
    return -1; // Offline
}

GRAVITY_SYSV_ABI std::int32_t sceNetSend(std::int32_t s, const void* msg, std::size_t len, std::int32_t flags) {
    (void)s; (void)msg; (void)len; (void)flags;
    return -1;
}

GRAVITY_SYSV_ABI std::int32_t sceNetRecv(std::int32_t s, void* buf, std::size_t len, std::int32_t flags) {
    (void)s; (void)buf; (void)len; (void)flags;
    return -1;
}

GRAVITY_SYSV_ABI std::int32_t sceNetBind(std::int32_t s, const void* addr, std::uint32_t addrlen) {
    (void)s; (void)addr; (void)addrlen;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetListen(std::int32_t s, std::int32_t backlog) {
    (void)s; (void)backlog;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetAccept(std::int32_t s, void* addr, std::uint32_t* addrlen) {
    (void)s; (void)addr; (void)addrlen;
    return -1;
}

GRAVITY_SYSV_ABI std::int32_t sceNetSetsockopt(std::int32_t s, std::int32_t level, std::int32_t optname, const void* optval, std::uint32_t optlen) {
    (void)s; (void)level; (void)optname; (void)optval; (void)optlen;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetEpollCreate(const char* name, std::int32_t flags) {
    (void)name; (void)flags;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNetEpollControl(std::int32_t epfd, std::int32_t op, std::int32_t fd, void* event) {
    (void)epfd; (void)op; (void)fd; (void)event;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetEpollWait(std::int32_t epfd, void* events, std::int32_t maxevents, std::int32_t timeout) {
    (void)epfd; (void)events; (void)maxevents; (void)timeout;
    return 0;
}

GRAVITY_SYSV_ABI std::int32_t sceNetEpollDestroy(std::int32_t epfd) {
    (void)epfd;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlInit() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlTerm() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetState(std::int32_t* state) {
    if (state && guest_memory::accessible(state, sizeof(*state))) {
        *state = 1; // SCE_NET_CTL_STATE_DISCONNECTED (safe offline single-player mode)
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlRegisterCallback(void* cb, void* arg, std::int32_t* cb_id) {
    (void)cb; (void)arg;
    if (cb_id && guest_memory::accessible(cb_id, sizeof(*cb_id))) {
        *cb_id = 1;
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlUnregisterCallback(std::int32_t cb_id) {
    (void)cb_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetInfo(std::int32_t code, void* info) {
    (void)code; (void)info;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetNatInfo(void* nat_info) {
    (void)nat_info;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlGetResult(std::int32_t code, std::int32_t* result) {
    (void)code;
    if (result && guest_memory::accessible(result, sizeof(*result))) {
        *result = 0;
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNetCtlCheckCallback() {
    return SCE_OK;
}

// =========================================================================
// libSceSsl
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceSslInit(std::size_t pool_size) {
    (void)pool_size;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceSslTerm() {
    return SCE_OK;
}

// =========================================================================
// libSceHttp
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceHttpInit(std::size_t pool_size) {
    (void)pool_size;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpTerm() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpCreateConnectionWithURL(std::int32_t tmpl_id, const char* url, std::int32_t is_keepalive) {
    (void)tmpl_id; (void)url; (void)is_keepalive;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpDeleteConnection(std::int32_t conn_id) {
    (void)conn_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpCreateRequestWithURL(std::int32_t conn_id, std::int32_t method, const char* path, std::uint64_t content_len) {
    (void)conn_id; (void)method; (void)path; (void)content_len;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpDeleteRequest(std::int32_t req_id) {
    (void)req_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpSendRequest(std::int32_t req_id, const void* post_data, std::size_t size) {
    (void)req_id; (void)post_data; (void)size;
    return -1; // Offline
}

GRAVITY_SYSV_ABI std::int32_t sceHttpCreateTemplate(const char* user_agent, std::int32_t http_ver, std::int32_t auto_redirect) {
    (void)user_agent; (void)http_ver; (void)auto_redirect;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpDeleteTemplate(std::int32_t tmpl_id) {
    (void)tmpl_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpGetStatusCode(std::int32_t req_id, std::int32_t* status_code) {
    (void)req_id;
    if (status_code && guest_memory::accessible(status_code, sizeof(*status_code))) {
        *status_code = 404;
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpGetResponseContentLength(std::int32_t req_id, std::int32_t* is_chunked, std::uint64_t* content_length) {
    (void)req_id;
    if (is_chunked && guest_memory::accessible(is_chunked, sizeof(*is_chunked))) *is_chunked = 0;
    if (content_length && guest_memory::accessible(content_length, sizeof(*content_length))) *content_length = 0;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpReadData(std::int32_t req_id, void* buf, std::size_t size) {
    (void)req_id; (void)buf; (void)size;
    return 0; // EOF
}

GRAVITY_SYSV_ABI std::int32_t sceHttpAddRequestHeader(std::int32_t req_id, const char* name, const char* value, std::int32_t mode) {
    (void)req_id; (void)name; (void)value; (void)mode;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceHttpUriEscape(char* out, std::size_t* out_len, const char* in, std::size_t in_len) {
    if (!out || !out_len || !in) return -1;
    std::size_t copy_len = (std::min)(*out_len, in_len);
    std::memcpy(out, in, copy_len);
    *out_len = copy_len;
    return SCE_OK;
}

// =========================================================================
// libSceNpScore
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpScoreCreateNpTitleCtx(void* ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreDeleteNpTitleCtx(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreCreateRequest(std::int32_t ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreDeleteRequest(std::int32_t req) {
    (void)req;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreRecordScore(std::int32_t req, std::int32_t board_id, std::int64_t score, const void* comment, std::size_t comment_size, const void* opt) {
    (void)req; (void)board_id; (void)score; (void)comment; (void)comment_size; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreRecordScoreAsync(std::int32_t req, std::int32_t board_id, std::int64_t score, const void* comment, std::size_t comment_size, const void* opt) {
    (void)req; (void)board_id; (void)score; (void)comment; (void)comment_size; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScorePollAsync(std::int32_t req, void* result) {
    (void)req; (void)result;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreAbortRequest(std::int32_t req) {
    (void)req;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetBoardInfo(std::int32_t req, std::int32_t board_id, void* info) {
    (void)req; (void)board_id; (void)info;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByRange(std::int32_t req, std::int32_t board_id, std::uint32_t start, std::uint32_t count, void* ranking) {
    (void)req; (void)board_id; (void)start; (void)count; (void)ranking;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByRangeAsync(std::int32_t req, std::int32_t board_id, std::uint32_t start, std::uint32_t count, void* ranking) {
    (void)req; (void)board_id; (void)start; (void)count; (void)ranking;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByNpId(std::int32_t req, std::int32_t board_id, const void* np_ids, std::uint32_t count, void* ranking) {
    (void)req; (void)board_id; (void)np_ids; (void)count; (void)ranking;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetRankingByNpIdAsync(std::int32_t req, std::int32_t board_id, const void* np_ids, std::uint32_t count, void* ranking) {
    (void)req; (void)board_id; (void)np_ids; (void)count; (void)ranking;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetFriendsRanking(std::int32_t req, std::int32_t board_id, void* ranking) {
    (void)req; (void)board_id; (void)ranking;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpScoreGetFriendsRankingAsync(std::int32_t req, std::int32_t board_id, void* ranking) {
    (void)req; (void)board_id; (void)ranking;
    return SCE_OK;
}

// =========================================================================
// libSceNpTus
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpTusCreateNpTitleCtx(void* ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusDeleteNpTitleCtx(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusCreateRequest(std::int32_t ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusDeleteRequest(std::int32_t req) {
    (void)req;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusSetData(std::int32_t req, std::int32_t slot_id, const void* data, std::size_t size, const void* opt) {
    (void)req; (void)slot_id; (void)data; (void)size; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusSetDataVUser(std::int32_t req, std::int32_t slot_id, const void* data, std::size_t size, const void* opt) {
    (void)req; (void)slot_id; (void)data; (void)size; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusGetData(std::int32_t req, std::int32_t slot_id, void* data, std::size_t size, const void* opt) {
    (void)req; (void)slot_id; (void)data; (void)size; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusGetDataVUser(std::int32_t req, std::int32_t slot_id, void* data, std::size_t size, const void* opt) {
    (void)req; (void)slot_id; (void)data; (void)size; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusSetMultiSlotVariable(std::int32_t req, const void* slots, std::uint32_t count, const void* opt) {
    (void)req; (void)slots; (void)count; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusSetMultiSlotVariableVUser(std::int32_t req, const void* slots, std::uint32_t count, const void* opt) {
    (void)req; (void)slots; (void)count; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusGetMultiSlotVariable(std::int32_t req, const void* slots, std::uint32_t count, void* result, const void* opt) {
    (void)req; (void)slots; (void)count; (void)result; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTusGetMultiSlotVariableVUser(std::int32_t req, const void* slots, std::uint32_t count, void* result, const void* opt) {
    (void)req; (void)slots; (void)count; (void)result; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpTssGetData(std::int32_t req, std::int32_t slot_id, void* data, std::size_t size, const void* opt) {
    (void)req; (void)slot_id; (void)data; (void)size; (void)opt;
    return SCE_OK;
}

// =========================================================================
// libSceNpUtility
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpLookupCreateTitleCtx(void* ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpLookupDeleteTitleCtx(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpLookupCreateRequest(std::int32_t ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpLookupDeleteRequest(std::int32_t req) {
    (void)req;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpLookupNpId(std::int32_t req, const void* np_id, void* result) {
    (void)req; (void)np_id; (void)result;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpBandwidthTestInitStart() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpBandwidthTestGetStatus(void* status) {
    (void)status;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpBandwidthTestShutdown() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCreateTitleCtx(void* ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterDeleteTitleCtx(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCreateRequest(std::int32_t ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCreateAsyncRequest(std::int32_t ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterDeleteRequest(std::int32_t req) {
    (void)req;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterAbortRequest(std::int32_t req) {
    (void)req;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterPollAsync(std::int32_t req, void* result) {
    (void)req; (void)result;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterCensorComment(std::int32_t req, const char* src, char* dst, std::size_t size) {
    (void)req;
    if (src && dst && size > 0) {
        std::size_t len = (std::min)(std::strlen(src), size - 1);
        std::memcpy(dst, src, len);
        dst[len] = '\0';
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpWordFilterSanitizeComment(std::int32_t req, const char* src, char* dst, std::size_t size) {
    (void)req;
    if (src && dst && size > 0) {
        std::size_t len = (std::min)(std::strlen(src), size - 1);
        std::memcpy(dst, src, len);
        dst[len] = '\0';
    }
    return SCE_OK;
}

// =========================================================================
// libSceNpAuth
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpAuthCreateRequest(std::int32_t ctx) {
    (void)ctx;
    return 1;
}

GRAVITY_SYSV_ABI std::int32_t sceNpAuthDeleteRequest(std::int32_t req) {
    (void)req;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpAuthGetAuthorizationCode(std::int32_t req, const void* param, void* code) {
    (void)req; (void)param; (void)code;
    return SCE_OK;
}

// =========================================================================
// libSceNpCommerce
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogInitialize() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogOpen(const void* param) {
    (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogTerminate() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogUpdateStatus() {
    return SCE_COMMON_DIALOG_STATUS_FINISHED;
}

GRAVITY_SYSV_ABI std::int32_t sceNpCommerceDialogGetResult(void* result) {
    (void)result;
    return SCE_OK;
}

// =========================================================================
// libSceNpMatching2
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2Initialize() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2Terminate() {
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetUserInfo(const void* info) {
    (void)info;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2ContextStart(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2ContextStop(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2DestroyContext(std::int32_t ctx) {
    (void)ctx;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2CreateContext(std::int32_t* ctx) {
    if (ctx && guest_memory::accessible(ctx, sizeof(*ctx))) *ctx = 1;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterContextCallback(std::int32_t ctx, void* cb, void* arg) {
    (void)ctx; (void)cb; (void)arg;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterRoomEventCallback(std::int32_t ctx, void* cb, void* arg) {
    (void)ctx; (void)cb; (void)arg;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterRoomMessageCallback(std::int32_t ctx, void* cb, void* arg) {
    (void)ctx; (void)cb; (void)arg;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2RegisterSignalingCallback(std::int32_t ctx, void* cb, void* arg) {
    (void)ctx; (void)cb; (void)arg;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetDefaultRequestOptParam(const void* opt) {
    (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetServerId(std::int32_t ctx, void* id) {
    (void)ctx; (void)id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2LeaveRoom(std::int32_t ctx, std::int64_t room_id) {
    (void)ctx; (void)room_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2JoinRoom(std::int32_t ctx, std::int64_t room_id, const void* opt) {
    (void)ctx; (void)room_id; (void)opt;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2CreateJoinRoom(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SearchRoom(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetUserInfoList(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetWorldInfoList(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SendRoomMessage(std::int32_t ctx, std::int64_t room_id, const void* msg, std::size_t size) {
    (void)ctx; (void)room_id; (void)msg; (void)size;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SendRoomChatMessage(std::int32_t ctx, std::int64_t room_id, const void* msg, std::size_t size) {
    (void)ctx; (void)room_id; (void)msg; (void)size;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetRoomDataExternalList(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetRoomDataInternal(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2GetRoomMemberDataInternal(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetRoomDataExternal(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetRoomDataInternal(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SetRoomMemberDataInternal(std::int32_t ctx, const void* param) {
    (void)ctx; (void)param;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2KickoutRoomMember(std::int32_t ctx, std::int64_t room_id, std::uint64_t member_id) {
    (void)ctx; (void)room_id; (void)member_id;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNpMatching2SignalingGetConnectionInfo(std::int32_t ctx, void* info) {
    (void)ctx; (void)info;
    return SCE_OK;
}

// =========================================================================
// Dialogs & Social / WebApi
// =========================================================================

GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogInitialize() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogOpen(const void* param) { (void)param; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogUpdateStatus() { return SCE_COMMON_DIALOG_STATUS_FINISHED; }
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogGetResult(void* result) { (void)result; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceGameCustomDataDialogTerminate() { return SCE_OK; }

GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogInitialize() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogOpen(const void* param) { (void)param; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogUpdateStatus() { return SCE_COMMON_DIALOG_STATUS_FINISHED; }
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogGetResult(void* result) { (void)result; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceInvitationDialogTerminate() { return SCE_OK; }

GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogInitialize() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogOpen(const void* param) { (void)param; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogUpdateStatus() { return SCE_COMMON_DIALOG_STATUS_FINISHED; }
GRAVITY_SYSV_ABI std::int32_t sceNpProfileDialogTerminate() { return SCE_OK; }

GRAVITY_SYSV_ABI std::int32_t sceNpSnsInitialize() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpSnsTerminate() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpSnsCheckCallback() { return SCE_OK; }

GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogInitialize() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogOpen(const void* param) { (void)param; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogUpdateStatus() { return SCE_COMMON_DIALOG_STATUS_FINISHED; }
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogGetResult(void* result) { (void)result; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpSnsFacebookDialogTerminate() { return SCE_OK; }

GRAVITY_SYSV_ABI std::int32_t sceNpWebApiInitialize() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateContext(void* ctx) { (void)ctx; return 1; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteContext(std::int32_t ctx) { (void)ctx; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiTerminate() { return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateRequest(std::int32_t ctx, const void* param) { (void)ctx; (void)param; return 1; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiSendRequest(std::int32_t req) { (void)req; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiAbortRequest(std::int32_t req) { (void)req; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteRequest(std::int32_t req) { (void)req; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiGetHttpStatusCode(std::int32_t req, std::int32_t* code) {
    (void)req;
    if (code && guest_memory::accessible(code, sizeof(*code))) *code = 404;
    return SCE_OK;
}
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiReadData(std::int32_t req, void* buf, std::size_t size) {
    (void)req; (void)buf; (void)size;
    return 0; // EOF
}
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateHandle(void* handle) { (void)handle; return 1; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreatePushEventFilter(void* filter) { (void)filter; return 1; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiCreateServicePushEventFilter(void* filter) { (void)filter; return 1; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteServicePushEventFilter(std::int32_t filter) { (void)filter; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeletePushEventFilter(std::int32_t filter) { (void)filter; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiDeleteHandle(std::int32_t handle) { (void)handle; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiGetHttpResponseHeaderValueLength(std::int32_t req, const char* name, std::size_t* len) {
    (void)req; (void)name;
    if (len && guest_memory::accessible(len, sizeof(*len))) *len = 0;
    return SCE_OK;
}
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiGetHttpResponseHeaderValue(std::int32_t req, const char* name, char* val, std::size_t len) {
    (void)req; (void)name; (void)val; (void)len;
    return SCE_OK;
}
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiRegisterPushEventCallback(void* cb, void* arg) { (void)cb; (void)arg; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiRegisterServicePushEventCallback(void* cb, void* arg) { (void)cb; (void)arg; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiUnregisterServicePushEventCallback(std::int32_t cb_id) { (void)cb_id; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiUnregisterPushEventCallback(std::int32_t cb_id) { (void)cb_id; return SCE_OK; }
GRAVITY_SYSV_ABI std::int32_t sceNpWebApiUtilityParseNpId(const char* str, void* np_id) { (void)str; (void)np_id; return SCE_OK; }

} // extern "C"

} // namespace gravity::hle::np
