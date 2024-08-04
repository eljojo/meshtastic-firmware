#pragma once
#include "ProtobufModule.h"
#include "NaraEntry.h"
#include "mesh/generated/meshtastic/nara.pb.h"

#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>
#include <map>

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

    void setLog(String log);
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NaraMessage *r) override;
    bool sendHaiku(NodeNum dest, char* haikuText, _meshtastic_NaraMessage_MessageType messageType, int signature);
    int gamesInProgress();

    String getNaraMessage(int16_t y);
    String getShortMessage();
    String getClosestNodeNames(int maxNodes);

  protected:
    bool firstTime = 1;
    virtual int32_t runOnce() override;

    int nodeCount = 0;

    void updateNodeCount();
    int messageNextNode();

    String getLongMessage();

    String hashMessage;
    String screenLog;
    String closestNodes;

    std::map<NodeNum, NaraEntry> naraDatabase;

};

extern NaraModule *naraModule;
