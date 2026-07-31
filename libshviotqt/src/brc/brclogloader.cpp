#include <shv/core/log.h>
#include <shv/iotqt/brc/brclogloader.h>
#include <shv/iotqt/rpc/clientconnection.h>
#include <shv/iotqt/rpc/rpccall.h>

#include <shv/chainpack/rpcmessage.h>

#include <QTimer>

namespace shv::iotqt::brc {

using namespace shv::iotqt::rpc;

namespace {
constexpr auto METH_DATE_SPAN = "dateSpan";
constexpr auto METH_FETCH = "fetch";
constexpr int CHUNK_RECORD_COUNT = 1000;
constexpr int CHUNK_DELAY_MS = 200;

enum class DateSpanParamKey {Since = 1, Until = 2, Now = 3};
enum DateSpanResultKey {DateSpanSinceId, DateSpanUntilId, DateSpanUntilDate};
enum RecordKey {RecordType = 0, RecordTimestamp = 1, RecordPath = 2, RecordSignal = 3, RecordSource = 4, RecordValue = 5, RecordAccessLevel = 6, RecordUserId = 7, RecordRepeat = 8, RecordId = 9, RecordRef = 10, RecordTimeJump = 60};
}

BrcLogLoader::BrcLogLoader(ClientConnection *connection, QObject *parent)
	: QObject(parent)
	, m_connection(connection)
{
}

void BrcLogLoader::getLog(const std::string &shv_path, const shv::chainpack::RpcValue::DateTime &since, const shv::chainpack::RpcValue::DateTime &until)
{
	m_stop = false;
	m_shvPath = shv_path;

	shvInfo() << "getlog" << shv_path;
	getDateSpan(since, until);
}

void BrcLogLoader::stop()
{
	m_stop = true;
}

void BrcLogLoader::getDateSpan(const shv::chainpack::RpcValue::DateTime &since, const shv::chainpack::RpcValue::DateTime &until)
{
	shv::chainpack::RpcValue::IMap params{
		{static_cast<int>(DateSpanParamKey::Since), since},
		{static_cast<int>(DateSpanParamKey::Until), until},
	};

	auto *date_span_call = RpcCall::create(m_connection)
								->setShvPath(m_shvPath)
								->setMethod(METH_DATE_SPAN)
								->setParams(params);
	connect(date_span_call, &RpcCall::result, this, [this](const shv::chainpack::RpcValue &res) {
		if (res.asList().size() != 3) {
			emitErrorAndDelete(tr("Failed to load log. Invalid dateSpan result. Column count:") + " " + QString::number(res.asList().size()));
			return;
		}

		int64_t since_id = res.asList().value(DateSpanResultKey::DateSpanSinceId).toInt64();
		int64_t until_id = res.asList().value(DateSpanResultKey::DateSpanUntilId).toInt64();

		if (since_id > until_id) {
			emitLogLoadedAndDelete({});
			return;
		}

		m_sinceId = since_id;
		m_lastValidId = until_id;
		fetchNextChunk(since_id);
	});
	connect(date_span_call, &RpcCall::error, this, [this](const shv::chainpack::RpcError &err) {
		emitErrorAndDelete(tr("Failed to load log. Error:") + " " + QString::fromStdString(err.message()));
	});
	date_span_call->start();
}

void BrcLogLoader::fetchNextChunk(int64_t next_id, int attempts_left)
{
	if (m_stop || next_id > m_lastValidId) {
		emitLogLoadedAndDelete({});
		return;
	}

	auto params = shv::chainpack::RpcValue::List{next_id, CHUNK_RECORD_COUNT};
	auto *fetch_call = RpcCall::create(m_connection)
							->setShvPath(m_shvPath)
							->setMethod(METH_FETCH)
							->setParams(params);
	connect(fetch_call, &RpcCall::result, this, [this, next_id](const shv::chainpack::RpcValue &result) {
		const auto &raw_records = result.asList();
		if (raw_records.empty()) {
			//empty result can mean either "no more data" or an ID hole wider than CHUNK_RECORD_COUNT, try the next window
			auto next_window = next_id + CHUNK_RECORD_COUNT;
			emit logLoaded({}, GetLogState::ReadingNextChunk, progressPercent(next_window - 1));
			QTimer::singleShot(CHUNK_DELAY_MS, this, [this, next_window]() {
				fetchNextChunk(next_window);
			});
			return;
		}

		shv::chainpack::RpcValue::List filtered;
		int64_t last_fetched_id = next_id - 1;
		bool reached_last_valid = false;

		for (const auto &record: raw_records) {
			//id may be omitted on the wire, defaulting to the previous record's id + 1
			last_fetched_id = record.asIMap().value(static_cast<int>(RecordKey::RecordId), last_fetched_id + 1).toInt64();

			if (last_fetched_id > m_lastValidId) {
				reached_last_valid = true;
				break;
			}
			filtered.push_back(record);
		}

		if (reached_last_valid || last_fetched_id >= m_lastValidId) {
			emitLogLoadedAndDelete(filtered);
			return;
		}

		emit logLoaded(filtered, GetLogState::ReadingNextChunk, progressPercent(last_fetched_id));
		auto resume_id = last_fetched_id + 1;
		QTimer::singleShot(CHUNK_DELAY_MS, this, [this, resume_id]() {
			fetchNextChunk(resume_id);
		});
	});
	connect(fetch_call, &RpcCall::error, this, [this, next_id, attempts_left = attempts_left - 1](const shv::chainpack::RpcError &err) {
		if (attempts_left > 0) {
			QTimer::singleShot(CHUNK_DELAY_MS, this, [this, next_id, attempts_left]() {
				fetchNextChunk(next_id, attempts_left);
			});
			return;
		}
		emitErrorAndDelete(tr("Failed to load log. Error:") + " " + QString::fromStdString(err.message()));
	});
	fetch_call->start();
}

void BrcLogLoader::emitLogLoadedAndDelete(const shv::chainpack::RpcValue &log)
{
	emit logLoaded(log, GetLogState::Finished, 100);
	deleteLater();
}

void BrcLogLoader::emitErrorAndDelete(const QString &err_msg)
{
	emit error(err_msg);
	deleteLater();
}

int BrcLogLoader::progressPercent(int64_t current_id) const
{
	if (m_lastValidId <= m_sinceId) {
		return 100;
	}
	auto fraction = static_cast<double>(current_id - m_sinceId) / static_cast<double>(m_lastValidId - m_sinceId);
	return qBound(0, static_cast<int>(fraction * 100), 100);
}

} // namespace shv::iotqt::brc
