#include "shvgetlogparams.h"

namespace cp = shv::chainpack;

namespace shv {
namespace core {
namespace utils {

//const char *ShvGetLogParams::KEY_HEADER_OPTIONS = "headerOptions";
const char *ShvGetLogParams::KEY_MAX_RECORD_COUNT_DEPRICATED = "maxRecordCount";
const char *ShvGetLogParams::KEY_RECORD_COUNT_LIMIT = "recordCountLimit";
const char *ShvGetLogParams::KEY_WITH_SNAPSHOT = "withSnapshot";
const char *ShvGetLogParams::KEY_WITH_PATHS_DICT = "withPathsDict";

//const char *ShvGetLogParams::KEY_WITH_UPTIME = "withUptime";
//static const char *KEY_WITH_TYPE_INFO = "withTypeInfo";

const char *ShvGetLogParams::KEY_WITH_SINCE = "since";
const char *ShvGetLogParams::KEY_WITH_UNTIL = "until";
const char *ShvGetLogParams::KEY_PATH_PATTERN_TYPE = "pathPatternType";
const char *ShvGetLogParams::KEY_PATH_PATTERN = "pathPattern";
const char *ShvGetLogParams::KEY_DOMAIN_PATTERN = "domainPattern";

static const char REG_EX[] = "regex";

ShvGetLogParams::ShvGetLogParams(const chainpack::RpcValue &opts)
	: ShvGetLogParams()
{
	*this = fromRpcValue(opts);
}

chainpack::RpcValue ShvGetLogParams::toRpcValue() const
{
	cp::RpcValue::Map m;
	if(since.isValid())
		m[KEY_WITH_SINCE] = since;
	if(until.isValid())
		m[KEY_WITH_UNTIL] = until;
	if(!pathPattern.empty()) {
		m[KEY_PATH_PATTERN] = pathPattern;
		if(pathPatternType == PatternType::RegEx)
			m[KEY_PATH_PATTERN_TYPE] = REG_EX;
	}
	if(!domainPattern.empty())
		m[KEY_DOMAIN_PATTERN] = domainPattern;
	//m[KEY_HEADER_OPTIONS] = headerOptions;
	m[KEY_RECORD_COUNT_LIMIT] = recordCountLimit;
	m[KEY_WITH_SNAPSHOT] = withSnapshot;
	m[KEY_WITH_PATHS_DICT] = withPathsDict;
	//m[KEY_WITH_UPTIME] = withUptime;
	return chainpack::RpcValue{m};
}

ShvGetLogParams ShvGetLogParams::fromRpcValue(const chainpack::RpcValue &v)
{
	ShvGetLogParams ret;
	const cp::RpcValue::Map &m = v.toMap();
	ret.since = m.value(KEY_WITH_SINCE);
	ret.until = m.value(KEY_WITH_UNTIL);
	ret.pathPattern = m.value(KEY_PATH_PATTERN).toString();
	ret.pathPatternType = m.value(KEY_PATH_PATTERN_TYPE).toString() == REG_EX? PatternType::RegEx: PatternType::WildCard;
	ret.domainPattern = m.value(KEY_DOMAIN_PATTERN).toString();
	ret.recordCountLimit = m.value(KEY_RECORD_COUNT_LIMIT, m.value(KEY_MAX_RECORD_COUNT_DEPRICATED, DEFAULT_RECORD_COUNT_LIMIT)).toInt();
	ret.withSnapshot = m.value(KEY_WITH_SNAPSHOT).toBool();
	ret.withPathsDict = m.value(KEY_WITH_PATHS_DICT).toBool();
	return ret;
}

} // namespace utils
} // namespace iotqt
} // namespace shv