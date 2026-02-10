//
// Created by pengx on 2025/9/26.
//

#include "xml_builder.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>

#include "pugixml.hpp"

/**
 * 添加通用 XML 声明: <?xml version="1.0" encoding="GB2312"?>
 * @param doc pugi::xml_document
 */
static void add_xml_declaration(pugi::xml_document& doc) {
    pugi::xml_node declaration = doc.prepend_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "GB2312";
}

std::string XmlBuilder::buildDeviceInfo(const std::string& sn, const std::string& device_code,
                                        const std::string& device_name,
                                        const std::string& serial_number) {
    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  构建设备信息 XML" << std::endl;
    std::cout << "│  CmdType: DeviceInfo" << std::endl;
    std::cout << "│  SN: " << sn.c_str() << std::endl;
    std::cout << "│  DeviceID: " << device_code.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    pugi::xml_document xml;

    // 添加 XML 声明：<?xml version="1.0" encoding="GB2312"?>
    add_xml_declaration(xml);

    // 根节点 <Response>
    pugi::xml_node root = xml.append_child("Response");

    root.append_child("CmdType").text() = "DeviceInfo";
    root.append_child("SN").text() = sn.c_str();
    root.append_child("DeviceID").text() = device_code.c_str();
    root.append_child("DeviceName").text() = device_name.c_str();
    root.append_child("Manufacturer").text() = "CasicGBDevice";
    root.append_child("Model").text() = "GBDevice";
    root.append_child("Firmware").text() = "1.0.0";
    root.append_child("SerialNumber").text() = serial_number.c_str();
    root.append_child("Status").text() = "ON";

    // 保存到字符串，使用 2 空格缩进，保持可读性
    std::ostringstream oss;
    xml.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);
    std::string result = oss.str();

    std::cout << "┌─────────── DeviceInfo XML ───────────┐" << std::endl;
    std::cout << result.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
    return result;
}

std::string XmlBuilder::buildCatalog(const std::string& sn, const std::string& device_code,
                                     const std::string& server_domain, double longitude, double latitude) {
    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  构建设备目录 XML" << std::endl;
    std::cout << "│  CmdType: Catalog" << std::endl;
    std::cout << "│  SN: " << sn.c_str() << std::endl;
    std::cout << "│  DeviceID: " << device_code.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    pugi::xml_document xml;

    // 添加 XML 声明
    add_xml_declaration(xml);

    // 根节点 <Response>
    pugi::xml_node root = xml.append_child("Response");

    root.append_child("CmdType").text() = "Catalog";
    root.append_child("SN").text() = sn.c_str();
    root.append_child("DeviceID").text() = device_code.c_str();

    // SumNum 和 DeviceList
    root.append_child("SumNum").text() = "1";

    pugi::xml_node deviceList = root.append_child("DeviceList");
    deviceList.append_attribute("Num") = "1";

    // 添加通道 Item
    pugi::xml_node item = deviceList.append_child("Item");

    // 正确生成通道ID：设备ID + "0001" (子设备编号)
    std::string channel_id = device_code.substr(0, 16) + "0001";
    item.append_child("DeviceID").text() = channel_id.c_str();
    item.append_child("Name").text() = "Channel01";
    item.append_child("Manufacturer").text() = "CasicGBDevice";
    item.append_child("Model").text() = "GBDevice";
    item.append_child("Owner").text() = "Pengxh";
    item.append_child("CivilCode").text() = server_domain.c_str();
    item.append_child("Address").text() = "";
    item.append_child("ParentID").text() = device_code.c_str();
    item.append_child("Parental").text() = "1";
    item.append_child("SafetyWay").text() = "0";
    item.append_child("RegisterWay").text() = "1";
    item.append_child("Secrecy").text() = "0";
    item.append_child("Status").text() = "ON";

    // 使用 setprecision 控制浮点数输出精度
    std::ostringstream lon_stream, lat_stream;
    lon_stream << std::fixed << std::setprecision(6) << longitude;
    lat_stream << std::fixed << std::setprecision(6) << latitude;

    item.append_child("Longitude").text() = lon_stream.str().c_str();
    item.append_child("Latitude").text() = lat_stream.str().c_str();
    item.append_child("Altitude").text() = "0";

    // 保存为格式化字符串（带缩进）
    std::ostringstream oss;
    xml.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);
    std::string result = oss.str();

    std::cout << "┌──────────── Catalog XML ─────────────┐" << std::endl;
    std::cout << result.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
    return result;
}

std::string XmlBuilder::buildHeartbeat(const std::string& sn, const std::string& device_code) {
    std::cout << "┌──────────────────────────────────────┐" << std::endl;
    std::cout << "│  构建心跳 XML" << std::endl;
    std::cout << "│  CmdType: Keepalive" << std::endl;
    std::cout << "│  SN: " << sn.c_str() << std::endl;
    std::cout << "│  DeviceID: " << device_code.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;

    pugi::xml_document xml;

    // 添加 XML 声明
    add_xml_declaration(xml);

    // 根节点 <Notify>
    pugi::xml_node root = xml.append_child("Notify");

    root.append_child("CmdType").text() = "Keepalive";
    root.append_child("SN").text() = sn.c_str();
    root.append_child("DeviceID").text() = device_code.c_str();
    root.append_child("Status").text() = "OK";

    // 保存到字符串，使用 2 空格缩进，保持可读性
    std::ostringstream oss;
    xml.save(oss, "  ", pugi::format_default, pugi::encoding_utf8);
    std::string result = oss.str();

    std::cout << "┌─────────── Keepalive XML ────────────┐" << std::endl;
    std::cout << result.c_str() << std::endl;
    std::cout << "└──────────────────────────────────────┘" << std::endl;
    return result;
}
