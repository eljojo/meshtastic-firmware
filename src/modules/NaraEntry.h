#pragma once
#include <cstdint>
#include "mesh/generated/meshtastic/nara.pb.h"
#include "ProtobufModule.h"
#include "MeshService.h"

enum NaraEntryStatus {
  UNCONTACTED,
  GAME_INVITE_SENT, // waiting to accept
  GAME_INVITE_RECEIVED, // waiting to respond
  GAME_ACCEPTED, // waiting to "do our turn"
  GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US,
  GAME_WAITING_FOR_OPPONENT_TURN,
  GAME_CHECKING_WHO_WON,
  GAME_WON,
  GAME_LOST,
  GAME_DRAW,
  GAME_ABANDONED
};

class NaraEntry {
  public:
    uint32_t lastInteraction;
    NodeNum nodeNum;
    NaraEntryStatus status;
    char ourText[32];
    char theirText[32];
    uint32_t ourSignature;
    uint32_t theirSignature;
    int lastSignatureCounter;

    NaraEntry() : lastInteraction(0), nodeNum(0), status(UNCONTACTED) {}

    NaraEntry(NodeNum nodeNum, NaraEntryStatus status) {
      this->nodeNum = nodeNum;
      this->status = status;
      this->lastInteraction = millis(); // or use time(nullptr) for actual time
      resetGame();
    }

    void handleMeshPacket(const meshtastic_MeshPacket& mp, meshtastic_NaraMessage* nm);

    void checkWhoWon();

    int processNextStep();

    bool sendGameInvite(NodeNum dest, char* haikuText);
    bool sendGameAccept(NodeNum dest, char* haikuText);
    bool sendGameMove(NodeNum dest, char* haikuText, int signature);

    int getPoints() {
      if(status == GAME_WON || status == GAME_DRAW) {
        return 3;
      } else if(status == GAME_LOST) {
        return 2;
      } else if(status == UNCONTACTED || status == GAME_INVITE_SENT) {
        return 0;
      } else {
        return 1;
      }
    }

    void resetGame() {
      // set theirText and ourText to empty strings
      memset(theirText, 0, sizeof(theirText));
      memset(ourText, 0, sizeof(ourText));

      ourSignature = 0;
      theirSignature = 0;
      lastSignatureCounter = 0;
    }

    bool isGameInProgress() {
      return status == GAME_ACCEPTED || status == GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US || status == GAME_WAITING_FOR_OPPONENT_TURN || status == GAME_CHECKING_WHO_WON;
    }

    String getStatusString() {
      switch(status) {
        case UNCONTACTED:
          return "UNCONTACTED";
        case GAME_INVITE_SENT:
          return "GAME_INVITE_SENT";
        case GAME_INVITE_RECEIVED:
          return "GAME_INVITE_RECEIVED";
        case GAME_ACCEPTED:
          return "GAME_ACCEPTED";
        case GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US:
          return "OPPONENT_IS_WAITING_FOR_US";
        case GAME_WAITING_FOR_OPPONENT_TURN:
          return "WAITING_FOR_OPPONENT";
        case GAME_CHECKING_WHO_WON:
          return "CHECKING_WHO_WON";
        case GAME_WON:
          return "GAME_WON";
        case GAME_LOST:
          return "GAME_LOST";
        case GAME_DRAW:
          return "GAME_DRAW";
        case GAME_ABANDONED:
          return "GAME_ABANDONED";
        default:
          return "UNKNOWN";
      }
    }

  protected:
    void setStatus(NaraEntryStatus status);
};
