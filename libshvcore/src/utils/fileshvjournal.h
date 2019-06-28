#ifndef SHV_CORE_UTILS_FILESHVJOURNAL_H
#define SHV_CORE_UTILS_FILESHVJOURNAL_H

#include "../shvcoreglobal.h"
#include "shvjournalgetlogparams.h"

#include <shv/chainpack/rpcvalue.h>

#include <functional>
#include <vector>

namespace shv {
namespace core {
namespace utils {

struct SHVCORE_DECL_EXPORT ShvJournalEntry
{
	std::string path;
	shv::chainpack::RpcValue value;
	//int64_t time = 0;
	uint16_t shortTime = 0;
	bool isShortTimeSet = false;

	ShvJournalEntry() {}
	ShvJournalEntry(std::string path, shv::chainpack::RpcValue value)
		: path(std::move(path))
		, value{value}
	{}
	ShvJournalEntry(std::string path, shv::chainpack::RpcValue value, uint16_t short_time)
		: path(std::move(path))
		, value{value}
		, shortTime(short_time)
		, isShortTimeSet(true)
	{}

	bool isValid() const {return !path.empty() && value.isValid();}
	void setShortTime(uint16_t short_time) {shortTime = short_time; isShortTimeSet = true;}
};

class SHVCORE_DECL_EXPORT FileShvJournal
{
public:
	static constexpr long DEFAULT_FILE_SIZE_LIMIT = 100 * 1024;
	static constexpr long DEFAULT_JOURNAL_SIZE_LIMIT = 100 * 100 * 1024;
	static constexpr int DEFAULT_GET_LOG_RECORD_COUNT_LIMIT = 100 * 1000;
	static constexpr int FILE_DIGITS = 6;
	static const char* FILE_EXT;
	static constexpr char FIELD_SEPARATOR = '\t';
	static constexpr char RECORD_SEPARATOR = '\n';

	SHVCORE_DECL_EXPORT static const char* KEY_NAME;
	SHVCORE_DECL_EXPORT static const char *KEY_RECORD_COUNT;
	SHVCORE_DECL_EXPORT static const char *KEY_PATHS_DICT;

	struct SHVCORE_DECL_EXPORT Column
	{
		enum Enum {
			Timestamp = 0,
			UpTime,
			Path,
			Value,
			ShortTime,
		};
		static const char* name(Enum e);
	};
public:
	using SnapShotFn = std::function<void (std::vector<ShvJournalEntry>&)>;

	FileShvJournal(std::string device_id, SnapShotFn snf);

	void setJournalDir(std::string s);
	const std::string& journalDir() const;
	void setFileSizeLimit(const std::string &n);
	void setFileSizeLimit(int64_t n) {m_fileSizeLimit = n;}
	int64_t fileSizeLimit() const { return m_fileSizeLimit;}
	void setJournalSizeLimit(const std::string &n);
	void setJournalSizeLimit(int64_t n) {m_journalSizeLimit = n;}
	int64_t journalSizeLimit() const { return m_journalSizeLimit;}
	void setDeviceId(std::string id) { m_deviceId = std::move(id); }
	void setDeviceType(std::string type) { m_deviceType = std::move(type); }
	void setTypeInfo(const shv::chainpack::RpcValue &i) { m_typeInfo = i; }

	void append(const ShvJournalEntry &entry, int64_t msec = 0);

	shv::chainpack::RpcValue getLog(const ShvJournalGetLogParams &params);
	//virtual std::string defaultJournaldir();
private:
	void checkJournalConsistecy();
	void rotateJournal();

	std::string fileNoToName(int n);
	void updateJournalStatus();
	void checkJournalDir();
	int64_t findLastEntryDateTime(const std::string &fn);

	void appendEntry(std::ofstream &out, int64_t msec, const ShvJournalEntry &e);

	std::string getLine(std::istream &in, char sep);
	static long toLong(const std::string &s);
private:
	std::string m_deviceId;
	std::string m_deviceType;
	shv::chainpack::RpcValue m_typeInfo;
	struct //JournalDirStatus
	{
		bool journalDirExists = false;
		int minFileNo = -1;
		int maxFileNo = -1;
		int64_t journalSize = -1;
		int64_t recentTimeStamp = 0;

		bool isConsistent() const {return recentTimeStamp > 0 && maxFileNo >= 0 && journalDirExists && journalSize >= 0;}
	} m_journalStatus;
	SnapShotFn m_snapShotFn;
	mutable std::string m_journalDir;
	int64_t m_fileSizeLimit = DEFAULT_FILE_SIZE_LIMIT;
	int64_t m_journalSizeLimit = DEFAULT_JOURNAL_SIZE_LIMIT;
	int m_getLogRecordCountLimit = DEFAULT_GET_LOG_RECORD_COUNT_LIMIT;
};

} // namespace utils
} // namespace core
} // namespace shv

#endif