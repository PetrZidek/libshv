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
	int64_t timeJumpSec = 0;	//only meaningful when type == TimeJump
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
	ret.timeJumpSec = getField(RecordField::TimeJump).toInt64();
	ret.data.timestamp = getField(RecordField::Timestamp).toDateTime();
	ret.data.path = getField(RecordField::Path).toString();
	ret.data.value = getField(RecordField::Value);
	ret.data.userId = getField(RecordField::UserId).toStdString();

	return ret;
}

//https://github.com/silicon-heaven/historyprovider-rs/pull/142 timestamps before the first timeJump
//are shifted by its offset, timestamps between two timeJumps are linearly interpolated between them,
//timestamps after the last timeJump are left unadjusted
void adjustTimestamps(QVector<RawLogRecord> &raw)
{
	constexpr int64_t MSECS_PER_SEC = 1000;

	QVector<int> tj_indices;
	for (int i = 0; i < raw.size(); ++i) {
		if (raw[i].type == RecordType::TimeJump) {
			tj_indices.push_back(i);
		}
	}
	if (tj_indices.isEmpty()) {
		return;
	}

	auto ts_msec = [](const RawLogRecord &r) { return r.data.timestamp.msecsSinceEpoch(); };
	auto apply_offset = [&](RawLogRecord &r, int64_t offset_msec) {
		if (r.type == RecordType::Normal || r.type == RecordType::Keep) {
			r.data.timestamp = shv::chainpack::RpcValue::DateTime::fromMSecsSinceEpoch(ts_msec(r) + offset_msec);
		}
	};

	int first_tj_idx = tj_indices.first();
	int64_t first_offset_msec = raw[first_tj_idx].timeJumpSec * MSECS_PER_SEC;
	for (int i = 0; i < first_tj_idx; ++i) {
		apply_offset(raw[i], first_offset_msec);
	}

	for (int k = 0; k + 1 < tj_indices.size(); ++k) {
		int i_a = tj_indices[k];
		int i_b = tj_indices[k + 1];
		int64_t ts_a = ts_msec(raw[i_a]);
		int64_t ts_b = ts_msec(raw[i_b]);
		int64_t offset_b_msec = raw[i_b].timeJumpSec * MSECS_PER_SEC;
		int64_t denom = ts_b - offset_b_msec - ts_a;
		if (denom <= 0) {
			continue;
		}
		for (int i = i_a + 1; i < i_b; ++i) {
			auto ti = ts_msec(raw[i]);
			apply_offset(raw[i], offset_b_msec * (ti - ts_a) / denom);
		}
	}
	//records after the last timeJump are left unadjusted
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

	adjustTimestamps(raw);

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
