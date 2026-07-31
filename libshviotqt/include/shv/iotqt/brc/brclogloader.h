#pragma once

#include <shv/iotqt/shviotqt_export.h>

#include <shv/coreqt/rpc.h>

#include <shv/chainpack/rpcvalue.h>

#include <QObject>
#include <QPointer>

namespace shv::iotqt::rpc { class ClientConnection; }

namespace shv::iotqt::brc {

class LIBSHVIOTQT_EXPORT BrcLogLoader : public QObject
{
	Q_OBJECT
public:
	enum class GetLogState {ReadingNextChunk, Finished};

	explicit BrcLogLoader(shv::iotqt::rpc::ClientConnection *connection, QObject *parent = nullptr);

	void getLog(const std::string &shv_path, const shv::chainpack::RpcValue::DateTime &since, const shv::chainpack::RpcValue::DateTime &until);
	void stop();

	Q_SIGNAL void logLoaded(const shv::chainpack::RpcValue &log, GetLogState state, int progress);
	Q_SIGNAL void error(const QString &err_msg);

private:
	void getDateSpan(const shv::chainpack::RpcValue::DateTime &since, const shv::chainpack::RpcValue::DateTime &until);
	void fetchNextChunk(int64_t next_id, int attempts_left = 3);
	void emitLogLoadedAndDelete(const shv::chainpack::RpcValue &log);
	void emitErrorAndDelete(const QString &err_msg);
	int progressPercent(int64_t current_id) const;

	QPointer<shv::iotqt::rpc::ClientConnection> m_connection;
	std::string m_shvPath;
	int64_t m_sinceId = 0;
	int64_t m_lastValidId = 0;
	bool m_stop = false;
};

} // namespace shv::iotqt::brc
