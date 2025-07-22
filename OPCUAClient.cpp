#include "OPCUAClient.hpp"


static void ff(UA_Client* client, UA_UInt32 subId, void* subContext, UA_UInt32 monId, void* monContext, UA_DataValue* value)
{
	UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Received Notification");

	UA_NodeId* ptr = (UA_NodeId*)monContext;

	UA_Int32 currentValue = *(UA_Int32*)(value->value.data);
	UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "SubId:%u, MonId:%u, Current Value: %d\n",
		subId, monId, currentValue);
}

void OPCUAClient::handler_DataChanged(UA_Client* client, UA_UInt32 subId, void* subContext, UA_UInt32 monId, void* monContext, UA_DataValue* value)
{
	UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Received Notification");

	CustomMontext* ptr = (CustomMontext*)monContext;
	UA_NodeId* nodeId = ptr->nodeId;
	OPCUAClient* self = ptr->client;
	self->update_func_(*nodeId, value->value);



	/*UA_Int32 currentValue = *(UA_Int32*)(value->value.data);
	UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "SubId:%u, MonId:%u, Current Value: %d\n",
		subId, monId, currentValue);*/
}

void OPCUAClient::add_monitored_item_to_variable()
{
	if(eles_.empty()) {
		UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "No elements to monitor.");
		return;
	}
	UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
	request.requestedPublishingInterval = 10;
	UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client_, request,
		NULL, NULL, NULL);
	UA_UInt32 subId = response.subscriptionId;
	if (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD)
	{
		UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Create subscription succeeded, id %u\n", subId);
	}
	
	std::vector<UA_MonitoredItemCreateRequest> items;
	std::vector<UA_UInt32> newMonitoredItemIds;
	std::vector<UA_Client_DataChangeNotificationCallback> callbacks;
	std::vector<UA_Client_DeleteMonitoredItemCallback> deleteCallbacks;
	std::vector<void *> contexts;
	/* monitor "ths answer" */
	for (auto& node : eles_) {
		items.emplace_back(UA_MonitoredItemCreateRequest_default(node));
		callbacks.emplace_back(&OPCUAClient::handler_DataChanged);
		deleteCallbacks.emplace_back(nullptr);
		CustomMontext* monContext = new CustomMontext();
		monContext->nodeId = &node;
		monContext->client = this;
		contexts.emplace_back((void *)monContext);
	}

	UA_CreateMonitoredItemsRequest createRequest;
	UA_CreateMonitoredItemsRequest_init(&createRequest);
	createRequest.subscriptionId = subId;
	createRequest.timestampsToReturn = UA_TIMESTAMPSTORETURN_BOTH;
	createRequest.itemsToCreate = items.data();
	createRequest.itemsToCreateSize = items.size();
	UA_CreateMonitoredItemsResponse createResponse =
		UA_Client_MonitoredItems_createDataChanges(client_, createRequest, contexts.data(),
			callbacks.data(), deleteCallbacks.data());


	for (uint32_t i = 0; i < createResponse.resultsSize; ++i)
	{
		if (createResponse.results->statusCode != UA_STATUSCODE_GOOD)
		{
			printf("==> error\n");
		}
	}
	//����������ָ��
	contexts_.resize(contexts.size());
	std::copy(contexts.begin(), contexts.end(), contexts_.begin());
}

OPCUAClient& OPCUAClient::getInstance()
{
	static OPCUAClient instance;
	return instance;
}

int OPCUAClient::init(const std::string server_ip, 
	const std::string port, 
	std::vector<UA_NodeId>& elements, 
	std::function<void(const UA_NodeId& node, const UA_Variant&)> update_func_)
{
	server_ip_ = server_ip;
	port_ = port;
	eles_.resize(elements.size());
	std::copy(elements.begin(), elements.end(), eles_.begin());
	this->update_func_ = update_func_;

	// ��ʼ��OPC UA�ͻ���
	client_ = UA_Client_new();
	if (!client_) {
		std::cerr << "Failed to create OPC UA client." << std::endl;
		return -1;
	}
	UA_ClientConfig_setDefault(UA_Client_getConfig(client_));
	// ���÷�������ַ�Ͷ˿�
	std::string uri = "opc.tcp://" + server_ip + ":" + port;
	UA_StatusCode status = UA_Client_connect(client_, uri.c_str());
	//UA_StatusCode status = UA_Client_connect(client_, "opc.tcp://192.168.10.26:4840/freeopcua/server/");
	//UA_StatusCode status = UA_Client_connect(client_, "opc.tcp://localhost:4840/");
	if (status != UA_STATUSCODE_GOOD) {
		std::cerr << "Failed to connect to OPC UA server: " << UA_StatusCode_name(status) << std::endl;
		UA_Client_delete(client_);
		return -1;
	}

	add_monitored_item_to_variable();

	// �������ݱ仯�ص�����
	client_func_ = [this]() {
		while (1) {
			{
				std::lock_guard<std::mutex> lock(running_mutex_);
				if (!this->running_) {
					break;
				}
			}

			{
				std::lock_guard<std::mutex> lock(client_mutex_);
				UA_StatusCode status = UA_Client_run_iterate(this->client_, 0);
				if (status != UA_STATUSCODE_GOOD) {
					UA_LOG_ERROR(UA_Log_Stdout, UA_LOGCATEGORY_CLIENT, "Client run iterate failed: %s", UA_StatusCode_name(status));
					break;
				}
			}
		}
	};

	return 0;
}

void OPCUAClient::run()
{
	if (!client_func_) {
		std::cout << "Task is not assigned yet!" << std::endl;
		return ;
	}
	
	client_thread_ = std::thread(client_func_);
}


void OPCUAClient::delete_client()
{
	{
		std::lock_guard<std::mutex> lock(running_mutex_);
		running_ = false;
	}

	client_thread_.join();
	{
		std::lock_guard<std::mutex> lock(client_mutex_);
		if (client_) {
			UA_Client_disconnect(client_);
			UA_Client_delete(client_);
			client_ = nullptr;
		}
	}
	for(auto& context : contexts_) {
		CustomMontext* monContext = (CustomMontext*)context;
		delete context;
	}
}

void OPCUAClient::set_running(bool run)
{
	std::lock_guard<std::mutex> lock(running_mutex_);
	running_ = run;
}
