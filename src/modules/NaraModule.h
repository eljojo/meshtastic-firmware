#pragma once
#include "ProtobufModule.h"
#include "mesh/generated/meshtastic/nara.pb.h"

#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <map>

enum NaraEntryStatus {
    UNCONTACTED,
    SENT_GREETING,
    RECEIVED_RESPONSE,
    PRESENT_RECEIVED
};

struct NaraEntry {
    NaraEntryStatus status;
};

class NaraModule : private concurrency::OSThread, public ProtobufModule<meshtastic_NaraMessage>
{
  public:
    NaraModule();

    virtual bool wantUIFrame() override;
#if !HAS_SCREEN
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
#else
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
#endif

    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NaraMessage *r) override;

    String getNaraMessage(int16_t y);
    String getShortMessage();
    String getClosestNodeNames(int maxNodes);

    bool sendInfo(NodeNum dest = NODENUM_BROADCAST);

  protected:
    bool firstTime = 1;
    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
     */
    virtual meshtastic_MeshPacket *allocReply() override;
    virtual int32_t runOnce() override;

    int nodeCount = 0;

    void updateNodeCount();
    bool messageNextNode();

    String getLongMessage();

    String hashMessage;

    std::map<NodeNum, NaraEntry> naraDatabase;

};

extern NaraModule *naraModule;
