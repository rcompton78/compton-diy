  // Set by the Firmware card; live_helpers.ts calls it on update entity events.
  var firmwareUpdateStateListener: ((state: string) => void) | null = null;

  function makeFirmwareCard() {
    // Firmware
    var fwBody = el("div", "fw-body");
    var versionLabel = textLabel("Installed", displayVersion(S.firmware || S.installed_version, "Dev"));
    var checkBtn = button("Check for Update", "btn btn-secondary btn-sm");
    var statusMsg = el("span", "fw-status");
    var checkWrap = el("div");
    checkWrap.className = "check-wrap";
    checkWrap.appendChild(statusMsg);
    checkWrap.appendChild(checkBtn);
    var versionBlock = el("div");
    versionBlock.appendChild(actionRow(versionLabel, checkWrap));
    fwBody.appendChild(versionBlock);

    var updatesSection = el("div", "fw-updates");
    var updateRow = el("div");
    updatesSection.appendChild(updateRow);
    fwBody.appendChild(updatesSection);

    function installFailed() {
      S.firmware_install_pending = false;
      // Redraw even if the key didn't change, so Install is never left
      // showing "Installing…" next to the failure message.
      renderedRowKey = null;
      renderUpdateRow();
      statusMsg.textContent = "Install failed \u2013 try again";
      statusMsg.style.color = "var(--danger)";
    }

    // Key of what the row last drew. Rebuilding replaces (and unfocuses) the
    // Install button, so skip it when nothing the row shows has changed.
    var renderedRowKey: string | null = null;

    function renderUpdateRow() {
      // Also covers installs this page didn't start (auto-update, Home
      // Assistant, or a reload mid-install), so Install can't be sent twice.
      var installing = !!S.firmware_install_pending || S.update_state === "INSTALLING";
      var key = [S.update_available ? "1" : "0", S.latest_version || "", installing ? "1" : "0"].join("|");
      if (key === renderedRowKey) return;
      renderedRowKey = key;
      updateRow.replaceChildren();
      if (!S.update_available) return;
      var label = textLabel("Stable", S.latest_version);
      var installBtn = button("Install", "btn btn-primary btn-sm", function () {
        S.firmware_install_pending = true;
        statusMsg.textContent = "";
        // Redraws the row with a disabled "Installing…" button. The new
        // button isn't focused, so isEditingSetting won't block the re-render
        // when the frame comes back on its new version.
        renderUpdateRow();
        // Not post(): its failure banner says a setting failed to save, and
        // this card reports a failed install itself.
        var installUrl = endpoints.update + "/install";
        fetch(installUrl, { method: "POST" })
          .then(function (r) {
            if (!r.ok) throw new Error("install_failed: " + r.status);
          })
          .catch(function (err) {
            console.error("POST " + installUrl + " error:", err);
            // The frame answers before it starts installing (the OTA is
            // deferred), so a rejected request means it never started.
            installFailed();
          });
      });
      if (installing) {
        installBtn.disabled = true;
        installBtn.textContent = "Installing\u2026";
      }
      updateRow.appendChild(actionRow(label, installBtn));
    }

    // A successful install reboots the frame. A failed one puts the update
    // entity back to "UPDATE AVAILABLE" (ESPHome may coalesce the brief
    // "INSTALLING" state away), and nothing else publishes that while an
    // install is pending.
    firmwareUpdateStateListener = function (state) {
      if (S.firmware_install_pending && state === "UPDATE AVAILABLE") {
        installFailed();
        return;
      }
      renderUpdateRow();
    };

    renderUpdateRow();

    checkBtn.onclick = function () {
      checkBtn.disabled = true;
      checkBtn.textContent = "Checking\u2026";
      statusMsg.textContent = "";
      post(endpoints.firmware_check + "/press")
        .then(function () {
          return new Promise(function (r) {
            setTimeout(r, 4000);
          });
        })
        .then(function () {
          return safeGet(endpoints.update);
        })
        .then(function (data) {
          // ESPHome's update entity has no latest_version field: the latest
          // version is its "value", and its state says whether that's newer.
          if (data) {
            applyEntityToState({
              id: "update/Firmware: Update",
              value: data.value,
              state: data.state,
              current_version: data.current_version,
              latest_version: data.latest_version
            });
          }
          renderUpdateRow();
          if (!S.update_available) {
            statusMsg.textContent = "Up to date";
            statusMsg.style.color = "var(--success)";
          }
        })
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          checkBtn.disabled = false;
          checkBtn.textContent = "Check for Update";
        });
    };

    var autoUpdateOptions = ["Disabled"].concat(productSettingOptions("update_frequency"));
    var currentAutoUpdate = S.auto_update ? S.update_frequency : "Disabled";
    var freqField = field("Auto updates");
    freqField.appendChild(
      selectFromOptions(autoUpdateOptions, currentAutoUpdate, function (v) {
        if (v === "Disabled") {
          saveSetting("auto_update", false);
        } else {
          saveSetting("auto_update", true);
          saveSetting("update_frequency", v);
        }
      })
    );
    fwBody.appendChild(freqField);

    var firmwareUrlStatus = el("div", "status");
    function setFirmwareUrlStatus(msg, ok) {
      setStatus(firmwareUrlStatus, msg, ok ? "green" : "red", ok ? 3000 : null);
    }

    function makeFirmwareUrlField(label, key, placeholder) {
      var f = field(label);
      var firmwareUrlInput = input("url", S[key], placeholder, productTextMaxLength(key, MAX_FIRMWARE_URL_LENGTH));
      var firmwareUrlError = makeFieldError();
      firmwareUrlInput.onchange = function () {
        var url = normalizeFirmwareManifestUrl(firmwareUrlInput.value);
        firmwareUrlError.textContent = "";
        if (url && !isValidHttpUrl(url)) {
          firmwareUrlError.textContent = "Use a full http:// or https:// URL";
          return;
        }
        saveSetting(key, url)
          .then(function (r) {
            if (!r || !r.ok) throw new Error("save_failed");
            return delayMs(500);
          })
          .then(function () {
            return safeGet(endpoints[key]);
          })
          .then(function (resp) {
            var saved = normalizeFirmwareManifestUrl((resp && (resp.value || resp.state)) || url);
            S[key] = saved;
            firmwareUrlInput.value = saved;
            setFirmwareUrlStatus("Update URL saved", true);
          })
          .catch(function () {
            setFirmwareUrlStatus("Failed to save update URL", false);
          });
      };
      f.appendChild(firmwareUrlInput);
      f.appendChild(firmwareUrlError);
      return f;
    }

    var advancedBody = el("div", "fw-advanced-body");
    var firmwareUrlsHint = el("div", "field-hint");
    firmwareUrlsHint.textContent = "Use a custom manifest to check and install firmware from another location.";
    advancedBody.appendChild(firmwareUrlsHint);
    advancedBody.appendChild(makeFirmwareUrlField(
      "Stable Manifest URL",
      "firmware_manifest_url",
      FIRMWARE_MANIFEST_URLS.stable
    ));
    advancedBody.appendChild(firmwareUrlStatus);

    var advancedPanel = document.createElement("details");
    advancedPanel.className = "inline-expander";
    var advancedSummary = document.createElement("summary");
    advancedSummary.textContent = "Advanced";
    advancedPanel.appendChild(advancedSummary);
    advancedPanel.appendChild(advancedBody);
    fwBody.appendChild(advancedPanel);

    return makeCollapsibleCard("Firmware", fwBody, true);
  }

  function makeWifiCard() {
    var wifiBody = el("div", "fw-body");

    wifiBody.appendChild(actionRow(
      textLabel("Current C6 firmware", displayVersion(S.c6_current_firmware, "Unknown")),
      document.createElement("span")
    ));

    var availableLabel = textLabel("Available firmware", displayVersion(S.c6_available_firmware, "Unknown"));
    var actionWrap = el("div");
    actionWrap.className = "check-wrap";
    var checkBtn = button("Check", "btn btn-secondary btn-sm", function () {
      checkBtn.disabled = true;
      checkBtn.textContent = "Checking...";
      post(endpoints.c6_firmware_check + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            checkBtn.disabled = false;
            checkBtn.textContent = "Check";
          }, 3000);
        });
    });
    var installBtn = button("Install", "btn btn-primary btn-sm", function () {
      installBtn.disabled = true;
      installBtn.textContent = "Installing...";
      post(endpoints.c6_firmware_install + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            installBtn.disabled = false;
            installBtn.textContent = "Install";
          }, 3000);
        });
    });
    actionWrap.appendChild(checkBtn);
    actionWrap.appendChild(installBtn);
    wifiBody.appendChild(actionRow(availableLabel, actionWrap));

    return makeCollapsibleCard("WiFi", wifiBody, true);
  }

  function makeDeviceRebootCard() {
    var rebootBody = el("div", "fw-body");
    var rebootLabel = textLabel("", "Device Reboot");
    var rebootBtn = button("Reboot Screen", "btn btn-secondary btn-sm", function () {
      rebootBtn.disabled = true;
      rebootBtn.textContent = "Rebooting...";
      post(endpoints.reboot_screen + "/press")
        .catch(function () {
          // Shared request helpers already surface failures in the UI.
        })
        .finally(function () {
          setTimeout(function () {
            rebootBtn.disabled = false;
            rebootBtn.textContent = "Reboot Screen";
          }, 3000);
        });
    });
    rebootBody.appendChild(actionRow(rebootLabel, rebootBtn));
    return makeCollapsibleCard("Device Reboot", rebootBody, true);
  }

  function makeDeveloperCard() {
    if (!developerPanelEnabledByUrl()) return null;
    var devBadge = makeBadge(S.developer_features_enabled);
    var devBody = el("div");
    devBody.appendChild(toggleSettingRow({
      label: "Enable in-development features",
      value: S.developer_features_enabled,
      getValue: function () { return S.developer_features_enabled; },
      setValue: function (value) { S.developer_features_enabled = value; },
      badge: devBadge,
      onChange: function () {
        saveSetting("developer_features_enabled", S.developer_features_enabled);
        renderSettings();
      }
    }).field);
    return makeCollapsibleCard("Developer", devBody, true, devBadge);
  }
