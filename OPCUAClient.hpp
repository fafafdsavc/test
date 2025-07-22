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

	/// ��ʼ���ͻ���
	/// @param server_ip: ������IP��ַ
	/// @param port: �������˿�
	/// @param elements: ��Ҫ������OPC UA�����ڵ�ID�б�
	/// @param update_func_: �û��Զ������ݱ仯�ص�����
	int init(const std::string server_ip,
		const std::string port, 
		std::vector<UA_NodeId>& elements, 
		std::function<void(const UA_NodeId& node,const UA_Variant&)> update_func_);

	/// ���пͻ���
	void run();

	/// ����server����ֵ
	/// T: ��������
	/// @param node �����ڵ�ID
	/// @param data ����ֵ
	/// @param type ���������������ο����е�UA_TYPES����
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
	void delete_client(); // ���ٿͻ���
	void set_running(bool run); // ��������״̬

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
	std::string server_ip_; // ��������ַ
	std::string port_; // �˿ں�
	std::vector<UA_NodeId> eles_; //����������server����
	std::vector<void*> contexts_; // ������
	UA_Client* client_ = nullptr; // OPC UA�ͻ���ʵ��
	std::function<void()> client_func_; // �ͻ��˺���
	std::function<void(const UA_NodeId& node, const UA_Variant&)> update_func_; // �û��Զ��崦��仯���ݺ���
	std::thread client_thread_; // �ͻ����߳�
	std::mutex running_mutex_; // ����״̬������
	std::mutex client_mutex_; // �ͻ��˻�����
	bool running_ = true; // �Ƿ���������
};