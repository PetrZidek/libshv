#ifndef SHV_CORE_UTILS_SHVALARM_H
#define SHV_CORE_UTILS_SHVALARM_H

#include "../shvcoreglobal.h"

#include <necrologlevel.h>

#include <string>
#include <vector>

namespace shv { namespace chainpack { class RpcValue; }}

namespace shv {
namespace core {
namespace utils {

class ShvTypeInfo;

class SHVCORE_DECL_EXPORT ShvAlarm {
public:
	using Severity = NecroLogLevel;
public:
	ShvAlarm();
	explicit ShvAlarm(const std::string &path, bool is_active = false, Severity severity = Severity::Invalid, int level = 0, const std::string &description = {}, const std::string &label = {});
public:
	static Severity severityFromString(const std::string &lvl);
	const char *severityName() const;
	bool isLessSevere(const ShvAlarm &a) const;
	bool operator==(const ShvAlarm &a) const;

	Severity severity() const { return m_severity; }
	void setSeverity(Severity severity) { m_severity = severity; }

	const std::string& path() const { return m_path; }
	void setPath(const std::string &path) { m_path = path; }

	const std::string& description() const { return m_description; }
	void setDescription(const std::string &description) { m_description = description; }

	const std::string& label() const { return m_label; }
	void setLabel(const std::string &label) { m_label = label; }

	int level() const { return m_level; }
	void setLevel(int level) { m_level = level; }

	bool isActive() const { return m_isActive; }
	void setIsActive(bool is_active) { m_isActive = is_active; }

	bool isValid() const { return !path().empty(); }

	shv::chainpack::RpcValue toRpcValue(bool all_fields_if_not_active = false) const;
	static ShvAlarm fromRpcValue(const shv::chainpack::RpcValue &rv);
	static std::vector<ShvAlarm> checkAlarms(const ShvTypeInfo &type_info, const std::string &shv_path, const shv::chainpack::RpcValue &value);
	static std::vector<ShvAlarm> checkAlarms(const ShvTypeInfo &type_info, const std::string &shv_path, const std::string &type_name, const shv::chainpack::RpcValue &value);
protected:
	std::string m_path;
	bool m_isActive = false;
	std::string m_description;
	std::string m_label;
	int m_level = 0;
	Severity m_severity = Severity::Invalid;
};

} // namespace utils
} // namespace core
} // namespace shv

#endif // SHV_CORE_UTILS_SHVALARM_H
