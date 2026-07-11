#pragma once

#include <atomic>
#include <functional>
#include <vector>

/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                osci_licensing
  vendor:            jameshball
  version:           0.1.0
  name:              osci-render licensing
  description:       Licensing and update helpers for osci-render products
  website:           https://osci-render.com
  license:           GPLv3
  minimumCppStandard: 20

  dependencies:      juce_core, juce_data_structures, juce_events, juce_cryptography, juce_gui_basics, juce_gui_extra, osci_gui, osci_render_core

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <osci_gui/osci_gui.h>
#include <osci_render_core/osci_render_core.h>

#include "license/osci_LicenseToken.h"
#include "network/osci_BackendClient.h"
#include "feedback/osci_FeedbackClient.h"
#include "feedback/osci_FeedbackImagePreviewOverlay.h"
#include "feedback/osci_FeedbackSettingsOverlay.h"
#include "feedback/osci_FeedbackSuccessOverlay.h"
#include "feedback/osci_FeedbackOverlay.h"
#include "state/osci_UpdateSettings.h"
#include "license/osci_LicenseManager.h"
#include "system/osci_HardwareInfo.h"
#include "system/osci_DawProcessDetector.h"
#include "update/osci_UpdateChecker.h"
#include "update/osci_FileHash.h"
#include "update/osci_Downloader.h"
#include "update/osci_PendingInstall.h"
#include "update/osci_InstallerLauncher.h"
