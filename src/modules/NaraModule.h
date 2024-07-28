#pragma once
#include "SinglePortModule.h"

#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */

class NaraModule : private concurrency::OSThread, public SinglePortModule
{
  public:
    /** Constructor
     * name is for debugging output
     */
    NaraModule() : concurrency::OSThread("NaraModule"), SinglePortModule("nara", meshtastic_PortNum_PRIVATE_APP) {}

    virtual bool wantUIFrame() override;
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    String getNaraMessage(int16_t y);
    String getShortMessage();
    String getClosestNodeNames(int maxNodes);

  protected:
    bool firstTime = 1;
    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
     */
    virtual meshtastic_MeshPacket *allocReply() override;
    virtual int32_t runOnce() override;

    int nodeCount = 0;

    void updateNodeCount();
    String getLongMessage();
};

extern NaraModule *naraModule;
