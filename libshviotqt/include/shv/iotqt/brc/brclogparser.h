#pragma once

#include <shv/iotqt/shviotqt_export.h>

#include <shv/chainpack/rpcvalue.h>

#include <QVector>

namespace shv::iotqt::brc {

struct BrcLogRecord {
	shv::chainpack::RpcValue::DateTime timestamp;
	std::string path;
	shv::chainpack::RpcValue value;
	std::string userId;
	bool snapshot = false;
};

class LIBSHVIOTQT_EXPORT BrcLogParser
{
public:
	//https://silicon-heaven.github.io/shv-doc/rpcmethods/history.html .records/*:fetch record IMap keys
	static QVector<BrcLogRecord> parseLog(const shv::chainpack::RpcValue &data);
};

} // namespace shv::iotqt::brc
