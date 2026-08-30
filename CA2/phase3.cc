#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/spectrum-module.h"
#include <string>

using namespace ns3;

void MacRxCallback(std::string context, Ptr<const Packet> packet) {
}

void SinrCallback(std::string context, double sinr) {
}

int main(int argc, char *argv[]) {

    uint32_t nSta = 40;
    double simTime = 10.0;
    double minDist = 5.0;
    double maxDist = 20.0;
    uint32_t packetSize = 1024;
    std::string interval = "0.5";
    

    CommandLine cmd;
    cmd.Parse(argc, argv);

    NodeContainer apNode;
    apNode.Create(1);
    NodeContainer staNodes;
    staNodes.Create(nSta);

    SpectrumWifiPhyHelper spectrumPhy;
    Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel>();
    spectrumChannel->AddPropagationLossModel(lossModel);
    Ptr<ConstantSpeedPropagationDelayModel> delayModel = CreateObject<ConstantSpeedPropagationDelayModel>();
    spectrumChannel->SetPropagationDelayModel(delayModel);
    spectrumPhy.SetChannel(spectrumChannel);

    spectrumPhy.SetErrorRateModel("ns3::YansErrorRateModel");
    spectrumPhy.Set("TxPowerStart", DoubleValue(16.0));
    spectrumPhy.Set("TxPowerEnd", DoubleValue(16.0));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("HeMcs11"),
                                 "ControlMode", StringValue("HeMcs0"));

    WifiMacHelper mac;
    Ssid ssid = Ssid("wifi6-network");

    mac.SetType("ns3::StaWifiMac",
                "Ssid", SsidValue(ssid),
                "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDevices = wifi.Install(spectrumPhy, mac, staNodes);

    mac.SetType("ns3::ApWifiMac",
                "Ssid", SsidValue(ssid));
    
    mac.SetMultiUserScheduler("ns3::RrMultiUserScheduler",
                              "EnableUlOfdma", BooleanValue(true),
                              "EnableBsrp", BooleanValue(true),
                              "UseCentral26TonesRus", BooleanValue(true));

    NetDeviceContainer apDevice = wifi.Install(spectrumPhy, mac, apNode);

    MobilityHelper mobilityAp;
    Ptr<ListPositionAllocator> apPosition = CreateObject<ListPositionAllocator>();
    apPosition->Add(Vector(0.0, 0.0, 3.0)); // ارتفاع AP روی 3 متر
    mobilityAp.SetPositionAllocator(apPosition);
    mobilityAp.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityAp.Install(apNode);

    MobilityHelper mobilitySta;
    mobilitySta.SetPositionAllocator("ns3::RandomDiscPositionAllocator",
                                     "X", StringValue("0.0"),
                                     "Y", StringValue("0.0"),
                                     "Z", StringValue("0.0"),
                                     "Rho", StringValue("ns3::UniformRandomVariable[Min=" + std::to_string(minDist) + "|Max=" + std::to_string(maxDist) + "]"));
    mobilitySta.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilitySta.Install(staNodes);

    InternetStackHelper stack;
    stack.Install(apNode);
    stack.Install(staNodes);

    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);

    uint16_t port = 5000;
    PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sinkApp = sink.Install(apNode.Get(0));
    sinkApp.Start(Seconds(0.0));
    sinkApp.Stop(Seconds(simTime));

    Ptr<UniformRandomVariable> startTime = CreateObject<UniformRandomVariable>();
    startTime->SetAttribute("Min", DoubleValue(0.0));
    startTime->SetAttribute("Max", DoubleValue(1.0));

    for (uint32_t i = 0; i < nSta; ++i) {
        UdpClientHelper client(apInterface.GetAddress(0), port);
        client.SetAttribute("MaxPackets", UintegerValue(10000));
        client.SetAttribute("Interval", TimeValue(Seconds(std::stod(interval))));
        client.SetAttribute("PacketSize", UintegerValue(packetSize));
        ApplicationContainer clientApp = client.Install(staNodes.Get(i));
        clientApp.Start(Seconds(startTime->GetValue()));
        clientApp.Stop(Seconds(simTime));
    }

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    std::cout << "Starting Simulation for " << simTime << " seconds...\n" << std::endl;
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    double totalThroughput = 0.0;
    Time totalDelay = Seconds(0.0);
    uint32_t totalRxPackets = 0;
    uint32_t totalTxPackets = 0;
    uint32_t validFlows = 0;

    for (auto i = stats.begin(); i != stats.end(); ++i) {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(i->first);
        
        if (t.destinationPort == port) {
            totalTxPackets += i->second.txPackets;
            totalRxPackets += i->second.rxPackets;
            totalThroughput += i->second.rxBytes * 8.0 / simTime / 1024 / 1024; // برحسب Mbps
            
            if (i->second.rxPackets > 0) {
                totalDelay += (i->second.delaySum / i->second.rxPackets);
                validFlows++;
            }
        }
    }

    
    std::cout << "Total Transmitted Packets : " << totalTxPackets << "\n";
    std::cout << "Total Received Packets    : " << totalRxPackets << "\n";
    std::cout << "Total Packet Loss         : " << (totalTxPackets - totalRxPackets) << "\n";
    std::cout << "Total Throughput          : " << totalThroughput << " Mbps\n";
    if (validFlows > 0) {
        std::cout << "Average Delay             : " << (totalDelay.GetSeconds() / validFlows) << " Seconds\n";
    }

    monitor->SerializeToXmlFile("phase3-flowmon.xml", true, true);

    Simulator::Destroy();
    return 0;
}