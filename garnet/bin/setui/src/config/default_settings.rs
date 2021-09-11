// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use serde::de::DeserializeOwned;
use std::fmt::{Debug, Display};
use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use std::sync::Arc;

use crate::config;
use crate::config::base::ConfigLoadInfo;
use crate::config::inspect_logger::InspectConfigLogger;
use crate::event;
use crate::message::base::{role, Audience};
use crate::service::{message::Messenger, Role};

pub struct DefaultSetting<T, P>
where
    T: DeserializeOwned + Clone + Debug,
    P: AsRef<Path> + Display,
{
    default_value: Option<T>,
    config_file_path: P,
    cached_value: Option<Option<T>>,
    config_inspect_logger: Option<Arc<Mutex<InspectConfigLogger>>>,
}

impl<T, P> DefaultSetting<T, P>
where
    T: DeserializeOwned + Clone + std::fmt::Debug,
    P: AsRef<Path> + Display,
{
    pub fn new(
        default_value: Option<T>,
        config_file_path: P,
        config_inspect_logger: Option<Arc<Mutex<InspectConfigLogger>>>,
    ) -> Self {
        DefaultSetting {
            default_value,
            config_file_path,
            cached_value: None,
            config_inspect_logger,
        }
    }

    /// Returns the value of this setting. Loads the value from storage if it hasn't been loaded
    /// before, otherwise returns a cached value.
    pub fn get_cached_value(&mut self) -> Result<Option<T>, Error> {
        if self.cached_value.is_none() {
            self.cached_value = Some(self.load_default_settings(None)?);
        }

        Ok(self.cached_value.as_ref().expect("cached value not present").clone())
    }

    /// Loads the value of this setting from storage.
    ///
    /// If the value isn't present, returns the default value.
    pub fn load_default_value(&mut self) -> Result<Option<T>, Error> {
        Ok(self.load_default_settings(None)?)
    }

    /// Loads the value of this setting from storage and reports the load results.
    ///
    /// If the value isn't present, returns the default value.
    pub fn load_default_value_and_report(
        &mut self,
        messenger: &Messenger,
    ) -> Result<Option<T>, Error> {
        Ok(self.load_default_settings(Some(messenger))?)
    }

    /// Attempts to load the settings from the given config_file_path.
    ///
    /// Returns the default value if unable to read or parse the file. The returned option will
    /// only be None if the default_value was provided as None.
    ///
    /// If a messenger is provided, the result of the load will be reported.
    fn load_default_settings(&mut self, messenger: Option<&Messenger>) -> Result<Option<T>, Error> {
        let config_load_info: Option<ConfigLoadInfo>;
        let path = self.config_file_path.to_string();
        let load_result = match File::open(self.config_file_path.as_ref()) {
            Ok(file) => {
                match serde_json::from_reader(BufReader::new(file)) {
                    Ok(config) => {
                        // Success path.
                        config_load_info = Some(ConfigLoadInfo {
                            path: path.clone(),
                            status: config::base::ConfigLoadStatus::Success,
                            contents: if let Some(ref payload) = config {
                                Some(format!("{:?}", payload))
                            } else {
                                None
                            },
                        });
                        Ok(config)
                    }
                    Err(e) => {
                        // Found file, but failed to parse.
                        let err_msg = format!("unable to parse config: {:?}", e);
                        config_load_info = Some(ConfigLoadInfo {
                            path: path.clone(),
                            status: config::base::ConfigLoadStatus::ParseFailure(err_msg.clone()),
                            contents: None,
                        });
                        Err(format_err!("{:?}", err_msg))
                    }
                }
            }
            Err(..) => {
                // No file found.
                config_load_info = Some(ConfigLoadInfo {
                    path: path.clone(),
                    status: config::base::ConfigLoadStatus::UsingDefaults(
                        "File not found, using defaults".to_string(),
                    ),
                    contents: None,
                });
                Ok(self.default_value.clone())
            }
        };
        if let Some(config_load_info) = config_load_info {
            if let Some(messenger) = messenger {
                self.report_config_load(messenger, config::base::Event::Load(config_load_info));
            } else {
                // No messenger provided, attempt to write directly to inspect.
                self.write_config_load_to_inspect(config_load_info);
            }
        } else {
            fx_log_err!("Could not load config for {:?}", path);
        }

        load_result
    }

    /// Sends an event reporting the results of the config load. The corresponding agent will be
    /// responsible for handling the message.
    fn report_config_load(&self, messenger: &Messenger, config_event: config::base::Event) {
        messenger
            .message(
                event::Payload::Event(event::Event::ConfigLoad(config_event)).into(),
                Audience::Role(role::Signature::role(Role::Event(event::Role::Sink))),
            )
            .send()
            .ack();
    }

    /// Attempts to write the config load to inspect. Requires a provided `config_inspect_logger`
    /// when calling new().
    fn write_config_load_to_inspect(&mut self, config_load_info: config::base::ConfigLoadInfo) {
        if let Some(inspect_logger) = self.config_inspect_logger.clone() {
            fasync::Task::spawn(async move {
                inspect_logger.lock().await.write_config_load_to_inspect(config_load_info);
            })
            .detach();
        } else {
            fx_log_err!(
                "Attempted to write config to inspect without a provided InspectConfigLogger"
            );
        }
    }
}

#[cfg(test)]
pub(crate) mod testing {
    use super::*;

    use crate::agent::Context;
    use crate::message::base::{filter, MessengerType};
    use crate::message::MessageHubUtil;
    use crate::service::{self, MessageHub};
    use crate::tests::message_utils::verify_payload;

    use serde::Deserialize;
    use std::collections::HashSet;

    #[derive(Clone, Debug, Deserialize)]
    struct TestConfigData {
        value: u32,
    }

    async fn create_context() -> Context {
        Context::new(
            MessageHub::create_hub()
                .create(MessengerType::Unbound)
                .await
                .expect("should be present")
                .1,
            MessageHub::create_hub(),
            HashSet::new(),
            HashSet::new(),
            None,
        )
        .await
    }

    #[test]
    fn test_load_valid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_config_data.json",
            None,
        );

        assert_eq!(
            setting.load_default_value().expect("Failed to get default value").unwrap().value,
            10
        );
    }

    #[test]
    fn test_load_invalid_config_data() {
        let mut setting = DefaultSetting::new(
            Some(TestConfigData { value: 3 }),
            "/config/data/fake_invalid_config_data.json",
            None,
        );
        assert!(setting.load_default_value().is_err());
    }

    #[test]
    fn test_load_invalid_config_file_path() {
        let mut setting = DefaultSetting::new(Some(TestConfigData { value: 3 }), "nuthatch", None);

        assert_eq!(
            setting.load_default_value().expect("Failed to get default value").unwrap().value,
            3
        );
    }

    #[test]
    fn test_load_default_none() {
        let mut setting = DefaultSetting::<TestConfigData, &str>::new(None, "nuthatch", None);

        assert!(setting.load_default_value().expect("Failed to get default value").is_none());
    }

    #[test]
    fn test_no_inspect_write() {
        let mut setting = DefaultSetting::<TestConfigData, &str>::new(None, "nuthatch", None);

        assert!(setting.load_default_value().expect("Failed to get default value").is_none());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_config_inspect_write() {
        let context = create_context().await;
        let (_, mut receptor) = context
            .delegate
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Audience(Audience::Role(role::Signature::role(Role::Event(
                    event::Role::Sink,
                )))),
            ))))
            .await
            .expect("could not create broker");

        let (messenger, _) = context.delegate.create(MessengerType::Unbound).await.unwrap();

        let mut setting = DefaultSetting::new(Some(TestConfigData { value: 3 }), "nuthatch", None);
        let _ = setting.load_default_value_and_report(&messenger);

        verify_payload(
            service::Payload::Event(event::Payload::Event(event::Event::ConfigLoad(
                config::base::Event::Load(config::base::ConfigLoadInfo {
                    path: "nuthatch".to_string(),
                    status: config::base::ConfigLoadStatus::UsingDefaults(
                        "File not found, using defaults".to_string(),
                    ),
                    contents: None,
                }),
            ))),
            &mut receptor,
            None,
        )
        .await;
    }
}
