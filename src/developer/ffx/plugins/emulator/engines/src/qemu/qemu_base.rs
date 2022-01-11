// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The qemu_base module encapsulates traits and functions specific
//! for engines using QEMU as the emulator platform.

use crate::{arg_templates::process_flag_template, serialization::SerializingEngine};
use anyhow::{anyhow, bail, Result};
use async_trait::async_trait;
use ffx_emulator_common::{config, config::FfxConfigWrapper, process};
use ffx_emulator_config::{
    ConsoleType, DeviceConfig, EmulatorConfiguration, EmulatorEngine, GuestConfig, LogLevel,
    NetworkingMode,
};
use shared_child::SharedChild;
use std::{fs, path::PathBuf, process::Command, str, sync::Arc};

/// QemuBasedEngine collects the interface for
/// emulator engine implementations that use
/// QEMU as the emulator.
/// This allows the implementation to be shared
/// across multiple engine types.
#[async_trait]
pub(crate) trait QemuBasedEngine: EmulatorEngine + SerializingEngine {
    /// Checks that the required files are present
    fn check_required_files(&self, guest: &GuestConfig) -> Result<()> {
        let kernel_path = &guest.kernel_image;
        let zbi_path = &guest.zbi_image;
        let fvm_path = &guest.fvm_image;

        if !kernel_path.exists() {
            bail!("kernel file {:?} does not exist.", kernel_path);
        }
        if !zbi_path.exists() {
            bail!("zbi file {:?} does not exist.", zbi_path);
        }
        if let Some(file_path) = &fvm_path {
            if !file_path.exists() {
                bail!("fvm file {:?} does not exist.", file_path);
            }
        }
        Ok(())
    }

    /// Stages the source image files in an instance specific directory.
    /// Also resizes the fvms to the desired size and adds the authorized
    /// keys to the zbi.
    /// Returns an updated GuestConfig instance with the file paths set to
    /// the instance paths.
    async fn stage_image_files(
        &self,
        instance_name: &str,
        guest_config: &GuestConfig,
        device_config: &DeviceConfig,
        config: &FfxConfigWrapper,
    ) -> Result<GuestConfig> {
        let mut updated_guest = guest_config.clone();

        // Create the data directory if needed.
        let mut instance_root: PathBuf = config.file(config::EMU_INSTANCE_ROOT_DIR).await?;
        instance_root.push(instance_name);
        fs::create_dir_all(&instance_root)?;

        let kernel_name = guest_config
            .kernel_image
            .file_name()
            .ok_or(anyhow!("cannot read kernel file name '{:?}'", guest_config.kernel_image));
        let kernel_path = instance_root.join(kernel_name?);
        fs::copy(&guest_config.kernel_image, &kernel_path).expect("cannot stage kernel file");

        let zbi_path = instance_root
            .join(guest_config.zbi_image.file_name().ok_or(anyhow!("cannot read zbi file name"))?);

        // Add the authorized public keys to the zbi image to enable SSH access to
        // the guest.
        Self::embed_authorized_keys(&guest_config.zbi_image, &zbi_path, config)
            .await
            .expect("cannot embed authorized keys");

        let fvm_path = match &guest_config.fvm_image {
            Some(src_fvm) => {
                let fvm_path = instance_root
                    .join(src_fvm.file_name().ok_or(anyhow!("cannot read fvm file name"))?);
                fs::copy(src_fvm, &fvm_path).expect("cannot stage fvm file");

                // Resize the fvm image if needed.
                let image_size = format!(
                    "{}{}",
                    device_config.storage.quantity,
                    device_config.storage.units.abbreviate()
                );
                let fvm_tool = config
                    .get_host_tool(config::FVM_HOST_TOOL)
                    .await
                    .expect("cannot locate fvm tool");
                let resize_result = Command::new(fvm_tool)
                    .arg(&fvm_path)
                    .arg("extend")
                    .arg("--length")
                    .arg(&image_size)
                    .output()?;

                if !resize_result.status.success() {
                    bail!("Error resizing fvm: {}", str::from_utf8(&resize_result.stderr)?);
                }
                Some(fvm_path)
            }
            None => None,
        };

        updated_guest.kernel_image = kernel_path;
        updated_guest.zbi_image = zbi_path;
        updated_guest.fvm_image = fvm_path;
        Ok(updated_guest)
    }

    async fn embed_authorized_keys(
        src: &PathBuf,
        dest: &PathBuf,
        config: &FfxConfigWrapper,
    ) -> Result<()> {
        let zbi_tool = config.get_host_tool(config::ZBI_HOST_TOOL).await?;
        let auth_keys = config.file(config::SSH_PUBLIC_KEY).await?;

        if src == dest {
            return Err(anyhow!("source and dest zbi paths cannot be the same."));
        }

        let mut replace_str = "data/ssh/authorized_keys=".to_owned();

        replace_str.push_str(auth_keys.to_str().ok_or(anyhow!("cannot to_str auth_keys path."))?);
        let auth_keys_output = Command::new(zbi_tool)
            .arg("-o")
            .arg(dest)
            .arg("--replace")
            .arg(src)
            .arg("-e")
            .arg(replace_str)
            .output()?;

        if !auth_keys_output.status.success() {
            bail!("Error embedding authorized_keys: {}", str::from_utf8(&auth_keys_output.stderr)?);
        }
        Ok(())
    }

    fn tap_available() -> Result<bool> {
        if std::env::consts::OS != "linux" {
            return Ok(false);
        }
        let output = Command::new("ip")
            .args(["tuntap", "show"])
            .output()
            .expect("failed to run ip to check tun/tap status");
        let ifname = String::from_utf8_lossy(&output.stdout);
        Ok(ifname != "")
    }

    fn validate_network_flags(&self, emu_config: &EmulatorConfiguration) -> Result<()> {
        if emu_config.host.networking == NetworkingMode::Tap && std::env::consts::OS == "macos" {
            eprintln!(
                "Tun/Tap networking mode is not currently supported on MacOS. \
                You may experience errors with your current configuration."
            );
        }
        if emu_config.host.networking == NetworkingMode::Tap {
            if !Self::tap_available()? {
                eprintln!("To use emu with networking on Linux, configure Tun/Tap:");
                eprintln!(
                    "  sudo ip tuntap add dev qemu mode tap user $USER && sudo ip link set qemu up"
                );
                bail!("Configure Tun/Tap on your host or disable networking.")
            }
        }
        Ok(())
    }

    async fn run(&mut self, emu_binary: &PathBuf) -> Result<i32> {
        if !emu_binary.exists() || !emu_binary.is_file() {
            bail!("Giving up finding emulator binary. Tried {:?}", emu_binary)
        }

        self.emu_config_mut().flags = process_flag_template(&self.emu_config())?;

        let mut emulator_cmd = self.build_emulator_cmd(&emu_binary)?;

        if self.emu_config().runtime.log_level == LogLevel::Verbose
            || self.emu_config().runtime.dry_run
        {
            println!("[emulator] Running emulator cmd: {:?}\n", emulator_cmd);
            println!("[emulator] Running with ENV: {:?}\n", emulator_cmd.get_envs());
            if self.emu_config().runtime.dry_run {
                return Ok(0);
            }
        }

        let shared_process = SharedChild::spawn(&mut emulator_cmd)?;
        let child_arc = Arc::new(shared_process);

        self.set_pid(child_arc.id());

        self.write_to_disk(&self.emu_config().runtime.instance_directory)?;

        if self.emu_config().runtime.console == ConsoleType::Monitor
            || self.emu_config().runtime.console == ConsoleType::Console
        {
            // When running with '--monitor' or '--console' mode, the user is directly interacting
            // with the emulator console, or the guest console. Therefore wait until the
            // execution of QEMU or AEMU terminates.
            match fuchsia_async::unblock(move || process::monitored_child_process(&child_arc)).await
            {
                Ok(_) => {
                    return Ok(0);
                }
                Err(e) => {
                    if let Some(shutdown_error) = self.shutdown().err() {
                        log::debug!(
                            "Error encountered in shutdown when handling failed launch: {:?}",
                            shutdown_error
                        );
                    }
                    bail!("Emulator launcher did not terminate properly, error: {}", e)
                }
            }
        } else if self.emu_config().runtime.debugger {
            let status = child_arc.wait()?;
            if !status.success() {
                let exit_code = status.code().unwrap_or_default();
                if exit_code != 0 {
                    bail!("Cannot start Fuchsia Emulator.")
                }
            }
        }
        Ok(0)
    }

    fn shutdown_emulator(&self) -> Result<()> {
        if self.is_running() {
            print!("Terminating running instance {:?}", self.get_pid());
            if let Some(terminate_error) = process::terminate(self.get_pid()).err() {
                log::debug!("Error encountered terminating process: {:?}", terminate_error);
            }
        }
        Ok(())
    }

    /// Access to the engine's pid field.
    fn set_pid(&mut self, pid: u32);
    fn get_pid(&self) -> u32;

    /// Access to the engine's emulator_configuration field.
    fn emu_config(&self) -> &EmulatorConfiguration;

    /// Mutable access to the engine's emulator_configuration field.
    fn emu_config_mut(&mut self) -> &mut EmulatorConfiguration;

    /// An engine-specific function for building a command-line from the emu_config.
    fn build_emulator_cmd(&self, emu_binary: &PathBuf) -> Result<Command>;
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_trait::async_trait;
    use ffx_emulator_config::EngineType;
    use serde::Serialize;
    use tempfile::tempdir;

    #[derive(Serialize)]
    struct TestEngine {}
    impl QemuBasedEngine for TestEngine {
        fn set_pid(&mut self, _pid: u32) {}
        fn get_pid(&self) -> u32 {
            todo!()
        }
        fn emu_config(&self) -> &EmulatorConfiguration {
            todo!()
        }
        fn emu_config_mut(&mut self) -> &mut EmulatorConfiguration {
            todo!()
        }
        fn build_emulator_cmd(&self, _emu_binary: &PathBuf) -> Result<Command> {
            todo!()
        }
    }
    #[async_trait]
    impl EmulatorEngine for TestEngine {
        async fn start(&mut self) -> Result<i32> {
            todo!()
        }
        fn shutdown(&self) -> Result<()> {
            todo!()
        }
        fn show(&self) {
            todo!()
        }
        fn validate(&self) -> Result<()> {
            todo!()
        }
        fn engine_type(&self) -> EngineType {
            EngineType::default()
        }
        fn is_running(&self) -> bool {
            false
        }
    }
    impl SerializingEngine for TestEngine {}

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_staging() {
        let temp = tempdir().expect("cannot get tempdir");
        let root = temp.path();

        let engine = TestEngine {};

        let instance_name = "test-instance";
        let mut guest = GuestConfig::default();
        let device = DeviceConfig::default();
        let mut config = FfxConfigWrapper::new();

        let kernel_path = root.join("kernel");
        let _kernel_file = fs::File::create(&kernel_path).expect("cannot create test kernel file.");
        let zbi_path = root.join("zbi");
        let _zbi_file = fs::File::create(&zbi_path).expect("cannot create test zbi file.");
        let fvm_path = root.join("fvm");
        let _fvm_file = fs::File::create(&fvm_path).expect("cannot create test fvm file.");
        let auth_keys_path = root.join("authorized_keys");
        let _auth_keys_file =
            fs::File::create(&auth_keys_path).expect("cannot create test auth keys file.");

        config.overrides.insert(config::FVM_HOST_TOOL, "echo".to_string());
        config.overrides.insert(config::ZBI_HOST_TOOL, "echo".to_string());
        config.overrides.insert(config::EMU_INSTANCE_ROOT_DIR, root.display().to_string());
        config.overrides.insert(config::SSH_PUBLIC_KEY, auth_keys_path.display().to_string());

        guest.kernel_image = kernel_path;
        guest.zbi_image = zbi_path;
        guest.fvm_image = Some(fvm_path);

        let updated = engine.stage_image_files(instance_name, &guest, &device, &config).await;

        assert!(updated.is_ok(), "expected OK got {:?}", updated.unwrap_err());

        let actual = updated.expect("cannot get update guest config");
        let expected = GuestConfig {
            kernel_image: root.join(instance_name).join("kernel"),
            zbi_image: root.join(instance_name).join("zbi"),
            fvm_image: Some(root.join(instance_name).join("fvm")),
            ..Default::default()
        };
        assert_eq!(actual, expected);
    }
}
