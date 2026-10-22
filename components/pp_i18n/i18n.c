#include "i18n.h"

static int s_lang;

/* zh strings render through the CJK fallback font in pp_ui/fonts; after adding characters run tools/gen_cjk_font.sh. */
static const char *const S[T_COUNT][2] = {
    [T_BUS] = { "Bus", "公交" },
    [T_NAV] = { "Navigate", "导航" },
    [T_SETTINGS] = { "Settings", "设置" },
    [T_BRIGHTNESS] = { "Brightness", "亮度" },
    [T_LANGUAGE] = { "Language", "语言" },
    [T_BATTERY] = { "Battery", "电池" },
    [T_API] = { "API", "接口" },
    [T_WIFI] = { "Wi-Fi", "Wi-Fi" },
    [T_PREVIEW] = { "ROUTE PREVIEW - not live position", "路线预览（非实时定位）" },
    [T_MOCK] = { "MOCK DATA", "模拟数据" },
    [T_LIVE] = { "LIVE", "实时" },
    [T_LOADING] = { "Loading...", "加载中..." },
    [T_ERR_NET] = { "No network", "网络不可用" },
    [T_ERR_API] = { "API error", "接口错误" },
    [T_MIN] = { "min", "分钟" },
    [T_ARR] = { "Arr", "到站" },
    [T_SAVE] = { "Save", "收藏" },
    [T_SAVED] = { "Saved", "已收藏" },
    [T_FULL] = { "List full", "收藏已满" },
    [T_NEW_STOP] = { "New stop", "输入站号" },
    [T_FROM] = { "From", "起点" },
    [T_TO] = { "To", "终点" },
    [T_GO] = { "Go", "出发" },
    [T_SCANNING] = { "Scanning...", "扫描中..." },
    [T_NO_NETWORKS] = { "No networks found", "未发现网络" },
    [T_PASSWORD] = { "Password (8+)", "密码（8位以上）" },
    [T_SHOW] = { "Show", "显示" },
    [T_HIDE] = { "Hide", "隐藏" },
    [T_NOT_SET] = { "Not set", "未设置" },
    [T_CONNECTING] = { "Connecting", "连接中" },
    [T_CONNECTED] = { "Connected", "已连接" },
    [T_SEAT] = { "seat", "有座" },
    [T_STAND] = { "stand", "站位" },
    [T_CROWDED] = { "full", "拥挤" },
    [T_LANG_NAME] = { "English", "中文" },
    [T_OPEN] = { "open", "开放" },
    [T_ERR_KEY] = { "Key rejected", "密钥无效" },
    [T_ERR_TOKEN] = { "Token expired", "令牌已过期" },
    [T_NO_BUSES] = { "No buses here", "该站暂无班车" },
    [T_WRONG_PW] = { "Wrong password?", "密码错误？" },
    [T_NO_AP] = { "Network not found", "找不到网络" },
    [T_CHARGING] = { "charging", "充电中" },
};

void tr_set_lang(int lang) { s_lang = lang ? 1 : 0; }
int tr_lang(void) { return s_lang; }
const char *tr(tr_id_t id) { return S[id][s_lang]; }
