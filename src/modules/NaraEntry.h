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
  GAME_ABANDONED,
  COOLDOWN
};

class NaraEntry {
  public:
    uint32_t lastInteraction;
    uint32_t cooldownUntil = 0;
    NodeNum nodeNum;
    NaraEntryStatus status;
    char ourText[32];
    char theirText[32];
    uint32_t ourSignature;
    uint32_t theirSignature;
    int lastSignatureCounter;
    int winCount = 0;
    int loseCount = 0;
    int drawCount = 0;
    int gameCount = 0;
    bool inviteSent = false;

    NaraEntry() : lastInteraction(0), nodeNum(0), status(UNCONTACTED) {}

    NaraEntry(NodeNum nodeNum, NaraEntryStatus status) {
      this->nodeNum = nodeNum;
      this->status = status;
      this->lastInteraction = millis(); // or use time(nullptr) for actual time
      screenLog = "";
      resetGame();
    }

    bool handleMeshPacket(const meshtastic_MeshPacket& mp, meshtastic_NaraMessage* nm);

    int processNextStep();
    String nodeName();

    bool sendGameInvite(NodeNum dest, char* haikuText);
    bool sendGameAccept(NodeNum dest, char* haikuText);
    bool sendGameMove(NodeNum dest, char* haikuText, int signature);
    bool lowPowerMode();

    int getPoints() {
      int invitePoints = inviteSent ? 1 : 0;
      return winCount * 3 + drawCount * 5 + loseCount + invitePoints;
    }

    void resetGame() {
      LOG_DEBUG("NARA resetGame for node %0x\n", nodeNum);
      // set theirText and ourText to empty strings
      memset(theirText, 0, sizeof(theirText));
      memset(ourText, 0, sizeof(ourText));

      ourSignature = 0;
      theirSignature = 0;
      lastSignatureCounter = 0;
    }

    int startGame();
    int playGameTurn();
    int checkDeadlines();
    void acceptGame(meshtastic_NaraMessage* nm);
    void processOtherTurn(meshtastic_NaraMessage* nm);
    void setLog(String log);

    bool isGameInProgress() {
      return status == GAME_ACCEPTED || status == GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US || status == GAME_WAITING_FOR_OPPONENT_TURN || status == GAME_CHECKING_WHO_WON;
    }

    bool gameJustEnded() {
      return (status == GAME_WON || status == GAME_LOST || status == GAME_DRAW) && interactedRecently();
    }

    String getLog() {
      return screenLog;
    }

    bool interactedRecently() {
      return millis() - lastInteraction < 30000;
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
        case COOLDOWN:
          return "COOLDOWN";
        default:
          return "UNKNOWN";
      }
    }

    String getTitle() {
      if(status == GAME_INVITE_SENT) {
        return "pinging " + nodeName();
      } else if(status == GAME_INVITE_RECEIVED || isGameInProgress() || gameJustEnded()) {
        return "Battle " + nodeName();
      } else if (status == COOLDOWN) {
        return "backing off... " + nodeName();
      } else {
        return "";
      }
    }

    int getStatusPriority() {
      switch(status) {
        case COOLDOWN: case UNCONTACTED:
          return 0;
        case GAME_INVITE_SENT:
          return 1;
        case GAME_INVITE_RECEIVED:
          return 2;
        case GAME_ACCEPTED: case GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US: case GAME_WAITING_FOR_OPPONENT_TURN: case GAME_ABANDONED:
          return 3;
        case GAME_CHECKING_WHO_WON: case GAME_WON: case GAME_LOST: case GAME_DRAW:
          return 4;
        default:
          return 0;
      }
    }

    void checkWhoWon() {
      LOG_DEBUG("NARA checking signatures against 0x%0x, our_signature=%d, their_signature=%d\n", nodeNum, ourSignature, theirSignature);
      if(ourSignature == theirSignature) {
        setStatus(GAME_DRAW);
      } else if(ourSignature <= 0) {
        setStatus(GAME_LOST);
      } else if(theirSignature == 0) {
        setStatus(GAME_WON);
      } else if(ourSignature < theirSignature) {
        setStatus(GAME_WON);
      } else {
        setStatus(GAME_LOST);
      }
    }

    void abandonWeirdGame() {
      setStatus(COOLDOWN);
      resetGame();
    }

    void addCooldownPeriod() {
      cooldownUntil = millis() + random(5 * 1000, 20 * 1000);
    }

  protected:
    void setStatus(NaraEntryStatus status);
    String screenLog;
};
