#include <shv/iotqt/brc/brclogparser.h>

#include <shv/core/log.h>

namespace shv::iotqt::brc {

namespace {

enum class RecordType {Normal = 1, Keep = 2, TimeJump = 3, TimeAbig = 4};

struct RawLogRecord {
	static constexpr int INVALID_REF = -1;
	static RawLogRecord fromRpcValue(const shv::chainpack::RpcValue &data);

	int ref = INVALID_REF;
	RecordType type = RecordType::Normal;
	BrcLogRecord data;
};

RawLogRecord RawLogRecord::fromRpcValue(const shv::chainpack::RpcValue &data)
{
	enum class RecordField {Type = 0, Timestamp = 1, Path = 2, Signal = 3, Source = 4, Value = 5, AccessLevel = 6, UserId = 7, Repeat = 8, Id = 9, Ref = 10, TimeJump = 60};

	RawLogRecord ret;

	auto getField = [&](const auto record_field, const shv::chainpack::RpcValue &default_value = {}) {
		return data.asIMap().value(static_cast<int>(record_field), default_value);
	};

	ret.ref = getField(RecordField::Ref, RawLogRecord::INVALID_REF).toInt();
	ret.type = static_cast<RecordType>(getField(RecordField::Type, static_cast<int>(RecordType::Normal)).toInt());
	ret.data.timestamp = getField(RecordField::Timestamp).toDateTime();
	ret.data.path = getField(RecordField::Path).toString();
	ret.data.value = getField(RecordField::Value);
	ret.data.userId = getField(RecordField::UserId).toStdString();

	return ret;
}

} // namespace

QVector<BrcLogRecord> BrcLogParser::parseLog(const shv::chainpack::RpcValue &data)
{
	QVector<RawLogRecord> raw;

	for (const auto &d: data.asList()) {
		auto record = RawLogRecord::fromRpcValue(d);

		if (record.ref != RawLogRecord::INVALID_REF) {
			int ref_record_index = raw.size() - (record.ref) - 1;	//record.ref == 0 means previous record
			if ((ref_record_index >= 0) && (ref_record_index < raw.size())) {
				const auto &src_record = raw.at(ref_record_index);
				record.data.path = src_record.data.path;
				record.type = src_record.type;
			}
			else {
				shvWarning() << "Cannot get reference to record:" << d.toCpon();
			}
		}
		record.data.snapshot = (record.type == RecordType::Keep);
		raw.push_back(record);
	}

	QVector<BrcLogRecord> ret;
	//timeJump/timeAbig records are device-clock bookkeeping, not real signal changes, discard them
	for (const auto &record: raw) {
		if (record.type != RecordType::TimeJump && record.type != RecordType::TimeAbig) {
			ret.push_back(record.data);
		}
	}

	return ret;
}

} // namespace shv::iotqt::brc
