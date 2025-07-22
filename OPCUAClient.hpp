#include "open62541.h"
#include <iostream>
#include <string>
#include <thread>
#include <queue>
#include <vector>
#include <functional>
#include <mutex>

class OPCUAClient;

typedef struct {
	UA_NodeId* nodeId;
	OPCUAClient* client;
} CustomMontext;

class OPCUAClient {
public:
	static OPCUAClient& getInstance();

	/// 初始化客户端
	/// @param server_ip: 服务器IP地址
	/// @param port: 服务器端口
	/// @param elements: 需要监听的OPC UA变量节点ID列表
	/// @param update_func_: 用户自定义数据变化回调函数
	int init(const std::string server_ip,
		const std::string port, 
		std::vector<UA_NodeId>& elements, 
		std::function<void(const UA_NodeId& node,const UA_Variant&)> update_func_);

	/// 运行客户端
	void run();

	/// 更新server变量值
	/// T: 数据类型
	/// @param node 变量节点ID
	/// @param data 变量值
	/// @param type 数据类型索引，参考库中的UA_TYPES数组
	template <typename T>
	int update_varible(UA_NodeId& node, const void* data, int type ) {
		if (type < 0 || type >= UA_TYPES_COUNT) {
			std::cerr << "Invalid type index: " << type << std::endl;
			return -1;
		}
		if (!client_) {
			std::cerr << "Client is not initialized." << std::endl;
			return -1;
		}

		std::lock_guard<std::mutex> lock(client_mutex_);
		T change = *((T*)data);
		UA_Variant newValue;
		UA_Variant_init(&newValue);
		UA_Variant_setScalar(&newValue, &change, &UA_TYPES[type]);
		UA_StatusCode retval = UA_Client_writeValueAttribute(client_, node, &newValue);
		if (retval != UA_STATUSCODE_GOOD) {
			return -1;
		}
		return 0;
	}
	void delete_client(); // 销毁客户端
	void set_running(bool run); // 设置运行状态

private:
	OPCUAClient() = default;
	~OPCUAClient() = default;
	OPCUAClient(const OPCUAClient&) = delete;
	OPCUAClient& operator=(const OPCUAClient&) = delete;
	
	static void handler_DataChanged(UA_Client* client, UA_UInt32 subId,
		void* subContext, UA_UInt32 monId,
		void* monContext, UA_DataValue* value);
	void add_monitored_item_to_variable();

public:
	
private:
	std::string server_ip_; // 服务器地址
	std::string port_; // 端口号
	std::vector<UA_NodeId> eles_; //监听的所有server变量
	std::vector<void*> contexts_; // 上下文
	UA_Client* client_ = nullptr; // OPC UA客户端实例
	std::function<void()> client_func_; // 客户端函数
	std::function<void(const UA_NodeId& node, const UA_Variant&)> update_func_; // 用户自定义处理变化数据函数
	std::thread client_thread_; // 客户端线程
	std::mutex running_mutex_; // 运行状态互斥锁
	std::mutex client_mutex_; // 客户端互斥锁
	bool running_ = true; // 是否正在运行
};