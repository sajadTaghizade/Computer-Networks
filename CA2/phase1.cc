#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

int main (int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse (argc, argv);

    NodeContainer apNode;
    apNode.Create (1);

    NodeContainer staNodes;
    staNodes.Create (5);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
    YansWifiPhyHelper phy;
    phy.SetChannel (channel.Create ());


    // set noise
    
    // phy.Set ("RxNoiseFigure", DoubleValue (60.0));


    WifiMacHelper mac;
    WifiHelper wifi;
    wifi.SetStandard (WIFI_STANDARD_80211ac);

    Ssid ssid = Ssid ("ns3-wifi-project");

    mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
    NetDeviceContainer apDevice = wifi.Install (phy, mac, apNode);

    mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid), "ActiveProbing", BooleanValue (false));
    NetDeviceContainer staDevices = wifi.Install (phy, mac, staNodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
    
    positionAlloc->Add (Vector (0.0, 0.0, 0.0));
    positionAlloc->Add (Vector (5.0, 0.0, 0.0));
    positionAlloc->Add (Vector (1.545, 4.755, 0.0));
    positionAlloc->Add (Vector (-4.045, 2.939, 0.0));
    positionAlloc->Add (Vector (-4.045, -2.939, 0.0));
    positionAlloc->Add (Vector (1.545, -4.755, 0.0));

    mobility.SetPositionAllocator (positionAlloc);
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (apNode);
    mobility.Install (staNodes);

    InternetStackHelper stack;
    stack.Install (apNode);
    stack.Install (staNodes);

    Ipv4AddressHelper address;
    address.SetBase ("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterface = address.Assign (apDevice);
    Ipv4InterfaceContainer staInterfaces = address.Assign (staDevices);

    UdpEchoServerHelper echoServer (9);
    ApplicationContainer serverApps = echoServer.Install (apNode.Get (0));
    serverApps.Start (Seconds (0.0));
    serverApps.Stop (Seconds (10.0));

    for (uint32_t i = 0; i < 5; ++i)
    {
        UdpEchoClientHelper echoClient (apInterface.GetAddress (0), 9);
        echoClient.SetAttribute ("MaxPackets", UintegerValue (1000));
        echoClient.SetAttribute ("Interval", TimeValue (Seconds (0.5)));
        
        if (i == 0 || i == 2 || i == 4)
        {
            echoClient.SetAttribute ("PacketSize", UintegerValue (1024));
        }
        else
        {
            echoClient.SetAttribute ("PacketSize", UintegerValue (512));
        }

        ApplicationContainer clientApps = echoClient.Install (staNodes.Get (i));
        clientApps.Start (Seconds (0.0));
        clientApps.Stop (Seconds (10.0));
    }

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    Simulator::Stop (Seconds (10.0));
    Simulator::Run ();

    monitor->CheckForLostPackets ();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

    double totalThroughput = 0.0;
    double totalThroughputSquared = 0.0;
    uint32_t flowCount = 0;

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
        if (t.destinationAddress == apInterface.GetAddress (0))
        {
            double throughput = i->second.rxBytes * 8.0 / 10.0 / 1024 / 1024; 
            double delay = i->second.delaySum.GetSeconds () / i->second.rxPackets;
            
            std::cout << "Flow ID: " << i->first << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
            std::cout << "Throughput: " << throughput << " Mbps\n";
            std::cout << "Mean Delay: " << delay << " s\n";
            
            double lossRatio = 0.0;
            if (i->second.txPackets > 0)
            {
                lossRatio = ((i->second.txPackets - i->second.rxPackets) * 100.0) / i->second.txPackets;
            }
            std::cout << "Packet Loss Ratio: " << lossRatio << " %\n\n";

            totalThroughput += throughput;
            totalThroughputSquared += (throughput * throughput);
            flowCount++;
        }
    }

    if (flowCount > 0)
    {
        double jainsIndex = (totalThroughput * totalThroughput) / (flowCount * totalThroughputSquared);
        std::cout << "--- Overall Stats ---\n";
        std::cout << "Average Throughput: " << (totalThroughput / flowCount) << " Mbps\n";
        std::cout << "Jain's Fairness Index: " << jainsIndex << "\n";
    }

    Simulator::Destroy ();
    return 0;
}