#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/quic-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/point-to-point-module.h"
#include <ns3/config-store.h>
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/mobility-module.h"
#include <vector>
#include <numeric>
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("QuicAggregationSimulation");

// 定义生产者数据生成函数
std::vector<int64_t> GenerateProducerData()
{
    std::vector<int64_t> data(1000);
    std::iota(data.begin(), data.end(), 1); // 生成1到1000的整数数据
    return data;
}

// 定义聚合器数据聚合函数
double AggregateData(const std::vector<std::vector<int64_t>>& data)
{
    std::vector<int64_t> allData;
    for (const auto& vec : data)
    {
        allData.insert(allData.end(), vec.begin(), vec.end());
    }
    int64_t sum = std::accumulate(allData.begin(), allData.end(), 0LL);
    return static_cast<double>(sum) / allData.size();
}

int main(int argc, char *argv[])
{
    // 仿真参数
    uint32_t numProducers = 30;
    uint32_t numAggregators = 6;
    uint32_t numConsumers = 1;
    double simulationTime = 20.0; // 秒
    uint32_t iterations = 1001;
   
    Config::SetDefault("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue(1 << 21));
    Config::SetDefault("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue(1 << 21));

    // 节点容器
    NodeContainer producers, aggregators, consumers;
    producers.Create(numProducers);
    aggregators.Create(numAggregators);
    consumers.Create(numConsumers);

    // 设置点对点连接和信道
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    // 连接生产者和聚合器
    NetDeviceContainer prodAggDevices[numProducers];
    for (uint32_t i = 0; i < numProducers; ++i)
    {
        prodAggDevices[i] = p2p.Install(producers.Get(i), aggregators.Get(i % numAggregators));
    }

    // 连接聚合器和消费者
    NetDeviceContainer aggConsDevices[numAggregators];
    for (uint32_t i = 0; i < numAggregators; ++i)
    {
        aggConsDevices[i] = p2p.Install(consumers.Get(0), aggregators.Get(i));
    }

    // 安装网络协议栈
    QuicHelper internet;
    internet.InstallQuic(producers);
    internet.InstallQuic(aggregators);
    internet.InstallQuic(consumers);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();

    // 设置生产者节点的方阵位置
    for (uint32_t i = 0; i < numProducers; ++i)
    {
        double x = (i % 6) * 10.0; // 每行6个节点，间隔10米
        double y = (i / 6) * 10.0;
        positionAlloc->Add(Vector(x, y, 0.0));
    }

    // 设置聚合器节点的方阵位置
    for (uint32_t i = 0; i < numAggregators; ++i)
    {
        double x = (i % 3) * 20.0 + 5.0; // 每行3个节点，间隔20米，偏移5米
        double y = (i / 3) * 20.0 + 5.0;
        positionAlloc->Add(Vector(x, y, 0.0));
    }

    // 设置消费者节点的位置
    positionAlloc->Add(Vector(50.0, 50.0, 0.0)); // 消费者在中心位置

    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(producers);
    mobility.Install(aggregators);
    mobility.Install(consumers);

    // 分配IP地址
    Ipv4AddressHelper address;
    Ipv4InterfaceContainer producerInterfaces[numProducers];
    Ipv4InterfaceContainer aggregatorInterfaces[numAggregators];
    Ipv4InterfaceContainer consumerInterfaces;

    for (uint32_t i = 0; i < numProducers; ++i)
    {
        std::ostringstream subnet;
        subnet << "10.1." << i + 1 << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        producerInterfaces[i] = address.Assign(prodAggDevices[i]);
    }

    for (uint32_t i = 0; i < numAggregators; ++i)
    {
        std::ostringstream subnet;
        subnet << "10.2." << i + 1 << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        aggregatorInterfaces[i] = address.Assign(aggConsDevices[i]);
    }

    // 生成生产者数据
    std::vector<std::vector<int64_t>> producerData(numProducers);
    for (uint32_t i = 0; i < numProducers; ++i)
    {
        producerData[i] = GenerateProducerData();
    }

    // 设置QUIC应用程序
    QuicServerHelper quicServer(1025);
    ApplicationContainer serverApps;
    ApplicationContainer clientApps;
    for (uint32_t i = 0; i < numAggregators; ++i)
    {
        serverApps.Add(quicServer.Install(aggregators.Get(i)));

        serverApps.Start(Seconds(1.0));
        serverApps.Stop(Seconds(simulationTime));

        QuicClientHelper quicClient(aggregatorInterfaces[i].GetAddress(1), 1025);
        quicClient.SetAttribute("MaxPackets", UintegerValue(10000000));
        clientApps.Add(quicClient.Install(consumers));
        clientApps.Start(Seconds(1.5));
        clientApps.Stop(Seconds(simulationTime));
    }

    QuicServerHelper quicServer1(1025);
    ApplicationContainer serverApps1;
    ApplicationContainer clientApps1;
    for (uint32_t i = 0; i < numProducers; ++i)
    {
        serverApps1.Add(quicServer1.Install(producers.Get(i)));

        serverApps1.Start(Seconds(1.0));
        serverApps1.Stop(Seconds(simulationTime));

        QuicClientHelper quicClient(producerInterfaces[i].GetAddress(1), 1025);
        quicClient.SetAttribute("MaxPackets", UintegerValue(10000000));
        for (uint32_t j = 0; j < numAggregators; ++j)
        {
            clientApps1.Add(quicClient.Install(aggregators.Get(j)));
        }
        clientApps1.Start(Seconds(1.5));
        clientApps1.Stop(Seconds(simulationTime));
    }

    double totalDirectTime = 0.0;
    double totalAggregatedTime = 0.0;


     FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
    // 迭代测试
    for (uint32_t iter = 1; iter < iterations; ++iter)
    {
        // 设置随机种子
        RngSeedManager::SetSeed(iter);
        RngSeedManager::SetRun(1);

        // 直接从生产者获取数据
        Simulator::Stop(Seconds(simulationTime));
        Simulator::Run();
        totalDirectTime += Simulator::Now().GetSeconds();

        // 从聚合器获取数据
        double totalAggregationTime = 0.0;
        for (uint32_t i = 0; i < numAggregators; ++i)
        {
            std::vector<std::vector<int64_t>> dataToAggregate;
            for (uint32_t j = 0; j < numProducers; ++j)
            {
                if (j % numAggregators == i)
                {
                    dataToAggregate.push_back(producerData[j]);
                }
            }
            totalAggregationTime += AggregateData(dataToAggregate);
        }
        totalAggregatedTime += totalAggregationTime / numAggregators;
    }

    // 输出结果
    double avgDirectTime = totalDirectTime / iterations;
    double avgAggregatedTime = totalAggregatedTime / iterations;

    std::cout << "Total Direct Time: " << totalDirectTime << " seconds" << std::endl;
    std::cout << "Average Direct Time: " << avgDirectTime << " seconds" << std::endl;
    std::cout << "Total Aggregated Time: " << totalAggregatedTime << " seconds" << std::endl;
    std::cout << "Average Aggregated Time: " << avgAggregatedTime << " seconds" << std::endl;

    // 输出流量监控结果
  
    flowMonitor->SerializeToXmlFile("flowmon.xml", true, true);
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowMonitor->GetFlowStats();

    for (auto &flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        std::cout << "Flow ID: " << flow.first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")" << std::endl;
        std::cout << "  Tx Packets: " << flow.second.txPackets << std::endl;
        std::cout << "  Rx Packets: " << flow.second.rxPackets << std::endl;
        std::cout << "  Packet Loss Rate: " << ((flow.second.txPackets - flow.second.rxPackets) / static_cast<double>(flow.second.txPackets)) * 100 << " %" << std::endl;
        std::cout << "  Average Aggregation Time: " << (flow.second.delaySum.GetSeconds() / flow.second.rxPackets) << " seconds" << std::endl;
    }

    Simulator::Destroy();
    return 0;
}
