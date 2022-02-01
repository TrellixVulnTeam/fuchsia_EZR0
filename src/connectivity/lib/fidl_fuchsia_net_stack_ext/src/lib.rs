// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod error;
pub use self::error::{FidlReturn, NetstackError};

use fidl_fuchsia_net_stack as fidl;

pub struct ForwardingEntry {
    pub subnet: fidl_fuchsia_net_ext::Subnet,
    pub device_id: u64,
    pub next_hop: Option<fidl_fuchsia_net_ext::IpAddress>,
    pub metric: u32,
}

impl From<fidl::ForwardingEntry> for ForwardingEntry {
    fn from(forwarding_entry: fidl::ForwardingEntry) -> Self {
        let fidl::ForwardingEntry { subnet, device_id, next_hop, metric } = forwarding_entry;
        let subnet = subnet.into();
        let next_hop = next_hop.map(|next_hop| (*next_hop).into());
        Self { subnet, device_id, next_hop, metric }
    }
}

pub enum AdministrativeStatus {
    DISABLED,
    ENABLED,
}

impl From<fidl::AdministrativeStatus> for AdministrativeStatus {
    fn from(administrative_status: fidl::AdministrativeStatus) -> Self {
        match administrative_status {
            fidl::AdministrativeStatus::Disabled => AdministrativeStatus::DISABLED,
            fidl::AdministrativeStatus::Enabled => AdministrativeStatus::ENABLED,
        }
    }
}

impl std::fmt::Display for AdministrativeStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        match self {
            AdministrativeStatus::DISABLED => write!(f, "DISABLED"),
            AdministrativeStatus::ENABLED => write!(f, "ENABLED"),
        }
    }
}

pub enum PhysicalStatus {
    UP,
    DOWN,
}

impl From<fidl::PhysicalStatus> for PhysicalStatus {
    fn from(physical_status: fidl::PhysicalStatus) -> Self {
        match physical_status {
            fidl::PhysicalStatus::Down => PhysicalStatus::DOWN,
            fidl::PhysicalStatus::Up => PhysicalStatus::UP,
        }
    }
}

impl std::fmt::Display for PhysicalStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        match self {
            PhysicalStatus::DOWN => write!(f, "LINK_DOWN"),
            PhysicalStatus::UP => write!(f, "LINK_UP"),
        }
    }
}

pub struct InterfaceProperties {
    pub name: String,
    pub topopath: String,
    pub filepath: String,
    pub mac: Option<fidl_fuchsia_hardware_ethernet_ext::MacAddress>,
    pub mtu: u32,
    pub features: fidl_fuchsia_hardware_ethernet::Features,
    pub administrative_status: AdministrativeStatus,
    pub physical_status: PhysicalStatus,
    pub addresses: Vec<fidl_fuchsia_net_ext::Subnet>,
}

impl From<fidl::InterfaceProperties> for InterfaceProperties {
    fn from(
        fidl::InterfaceProperties {
            name,
            topopath,
            filepath,
            mac,
            mtu,
            features,
            administrative_status,
            physical_status,
            addresses,
        }: fidl::InterfaceProperties,
    ) -> Self {
        let mac = mac.map(|mac| (*mac).into());
        let administrative_status = AdministrativeStatus::from(administrative_status);
        let physical_status = PhysicalStatus::from(physical_status);
        let addresses = addresses.into_iter().map(Into::into).collect();
        Self {
            name,
            topopath,
            filepath,
            mac,
            mtu,
            features,
            administrative_status,
            physical_status,
            addresses,
        }
    }
}

impl std::fmt::Display for InterfaceProperties {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let InterfaceProperties {
            name,
            topopath,
            filepath,
            mac,
            mtu,
            features,
            administrative_status,
            physical_status,
            addresses,
        } = self;
        write!(f, "  {:10} : {}\n", "name", name)?;
        write!(f, "  {:10} : {}\n", "topopath", topopath)?;
        write!(f, "  {:10} : {}\n", "filepath", filepath)?;
        if let Some(mac) = mac {
            write!(f, "  {:10} : {}\n", "mac", mac)?;
        } else {
            write!(f, "  {:10} : {}\n", "mac", "-")?;
        }
        write!(f, "  {:10} : {}\n", "mtu", mtu)?;
        write!(f, "  {:10} : {:?}\n", "features", features)?;
        write!(f, "  {:10} : {} | {}\n", "status", administrative_status, physical_status)?;
        for (idx, addr) in addresses.iter().enumerate() {
            if idx != 0 {
                write!(f, "\n")?;
            }
            write!(f, "  {:10} : {}", "addr", addr)?;
        }
        Ok(())
    }
}

pub struct InterfaceInfo {
    pub id: u64,
    pub properties: InterfaceProperties,
}

impl From<fidl::InterfaceInfo> for InterfaceInfo {
    fn from(fidl::InterfaceInfo { id, properties }: fidl::InterfaceInfo) -> Self {
        let properties = InterfaceProperties::from(properties);
        Self { id, properties }
    }
}

impl std::fmt::Display for InterfaceInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let InterfaceInfo { id, properties } = self;
        write!(f, "Network interface ID {}\n", id)?;
        write!(f, "{}", properties)?;
        Ok(())
    }
}

#[test]
fn test_display_interfaceinfo() {
    assert_eq!(
        &format!(
            "{}",
            InterfaceInfo {
                id: 1,
                properties: InterfaceProperties {
                    name: "eth000".to_owned(),
                    topopath: "/all/the/way/home".to_owned(),
                    filepath: "/dev/class/ethernet/123".to_owned(),
                    mac: Some(fidl_fuchsia_hardware_ethernet_ext::MacAddress {
                        octets: [0, 1, 2, 255, 254, 253]
                    }),
                    mtu: 1500,
                    features: fidl_fuchsia_hardware_ethernet::Features::all(),
                    administrative_status: AdministrativeStatus::ENABLED,
                    physical_status: PhysicalStatus::UP,
                    addresses: vec![
                        net_declare::fidl_subnet!("255.255.255.0/4").into(),
                        net_declare::fidl_subnet!("255.255.255.1/4").into(),
                    ],
                }
            }
        ),
        r#"Network interface ID 1
  name       : eth000
  topopath   : /all/the/way/home
  filepath   : /dev/class/ethernet/123
  mac        : 00:01:02:ff:fe:fd
  mtu        : 1500
  features   : WLAN | Wlan | SYNTHETIC | Synthetic | LOOPBACK | Loopback
  status     : ENABLED | LINK_UP
  addr       : 255.255.255.0/4
  addr       : 255.255.255.1/4"#
    );
}
