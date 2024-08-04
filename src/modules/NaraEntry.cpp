#include "NaraEntry.h"
#include "NaraModule.h"
#include "ProtobufModule.h"
#include "PowerStatus.h"

#define NUM_ZEROES 4                    // for hashcash
#define HASH_TURN_SIZE 1000             // size of hashcash turn before yielding thread
#define MAX_HASHCASH 100000             // give up game after these many iterations
#define GAME_GHOST_TTL 60000           // assume ghosted after 60 seconds
#define DRAW_RETRY_TIME_MS 5000            // retry in 5 seconds in case of draw
#define PLAY_EVERY_MS 2 * 60 * 1000        // play new game every 2 minutes
#define REINVITE_AFTER 10 * 60 * 1000      // reinvite after 10 minutes

bool NaraEntry::handleMeshPacket(const meshtastic_MeshPacket& mp, meshtastic_NaraMessage* nm) {
  if(nm->type == meshtastic_NaraMessage_MessageType_GAME_INVITE || nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT) {
    acceptGame(nm);
    return true;
  } else if(nm->type == meshtastic_NaraMessage_MessageType_GAME_TURN) {
    processOtherTurn(nm);
    return true;
  } else {
    LOG_WARN("NARA Received unexpected message from 0x%0x, type=%d. We're in status=%s\n", nodeNum, nm->type, getStatusString().c_str());
    if(isGameInProgress()) {
      abandonWeirdGame();
      return true;
    }
  }
  return false;
}

void NaraEntry::acceptGame(meshtastic_NaraMessage* nm) {
  // if we're not expecting a new game, something went wrong
  if(isGameInProgress() || (nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT && status != GAME_INVITE_SENT)) {
    LOG_WARN("NARA won't accept game from 0x%0x. Message type %d. We're in status=%s\n", nodeNum, nm->type, getStatusString().c_str());
    abandonWeirdGame();
    return;
  }

  strncpy(theirText, nm->haiku.text, sizeof(theirText) - 1);
  theirText[sizeof(theirText) - 1] = '\0';

  if(nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT && status == GAME_INVITE_SENT) {
    setStatus(GAME_ACCEPTED);
  } else {
    setStatus(GAME_INVITE_RECEIVED);
  }
}

void NaraEntry::processOtherTurn(meshtastic_NaraMessage* nm) {
    theirSignature = nm->haiku.signature;

    // TODO: check if their text is the same as our text (minus node num)
    // we could be in either GAME_ACCEPTED or GAME_WAITING_FOR_OPPONENT_TURN
    // we only switch status if we haven't sent our turn yet
    if(status == GAME_WAITING_FOR_OPPONENT_TURN) {
      setStatus(GAME_CHECKING_WHO_WON);
      checkWhoWon();
      resetGame();
    } else if(status == GAME_ACCEPTED) {
      LOG_INFO("NARA Received game turn from 0x%0x before we sent ours. Will switch on next iteration.\n", nodeNum);
      setStatus(GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US);
    } else {
      LOG_WARN("NARA Received game turn from 0x%0x, but we're not waiting for it. We're in status=%s\n", nodeNum, getStatusString().c_str());
      if(status == GAME_INVITE_SENT) {
        // looks like we missed the packet when the game was accepted, we could still try decoding the other nara's move and continue playing
      }
      abandonWeirdGame();
    }
}

void NaraEntry::setStatus(NaraEntryStatus status) {
  this->status = status;
  lastInteraction = millis();

  switch(status) {
    case UNCONTACTED:
      resetGame();
      break;
    case GAME_INVITE_SENT:
      if(nodeDB->getNodeNum() < nodeNum) {
        setLog("sent challenge " + String(ourText) + "/?");
      } else {
        setLog("sent challenge ?/" + String(ourText));
      }
      inviteSent = true;
      break;
    case GAME_INVITE_RECEIVED:
      if(nodeDB->getNodeNum() < nodeNum) {
        setLog("CHALLENGED!   ?/" + String(theirText));
      } else {
        setLog("CHALLENGED!   " + String(theirText) + "/?");
      }
      break;
    case GAME_ACCEPTED:
      if(nodeDB->getNodeNum() < nodeNum) {
        setLog(String("thinking turn...    ") + String(ourText) + "/" + String(theirText));
      } else {
        setLog(String("thinking turn...    ") + String(theirText) + "/" + String(ourText));
      }
      break;
    case GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US:
      setLog(nodeName() + " is waiting for us");
      break;
    case GAME_WAITING_FOR_OPPONENT_TURN:
      setLog("waiting for opponent");
      break;
    case GAME_CHECKING_WHO_WON:
      setLog("checking who won...");
      gameCount++;
      break;
    case GAME_WON:
      if(theirSignature == 0) {
        setLog("they conceded, we win!");
      } else {
        setLog("WON!      " + String(ourSignature) + " vs " + String(theirSignature));
      }
      winCount++;
      break;
    case GAME_LOST:
      if(ourSignature == 0) {
        setLog("conceding, " + nodeName() + " wins");
      } else {
        setLog("LOST!    " + String(ourSignature) + " vs " + String(theirSignature));
      }
      loseCount++;
      break;
    case GAME_DRAW:
      setLog("DRAW game");
      drawCount++;
      break;
    case GAME_ABANDONED:
      setLog(nodeName() + " GHOSTED us");
      break;
    case COOLDOWN:
      setLog(String("uh-oh! ") + String(nodeNum, HEX));
      break;
  }

  LOG_INFO("NARA node %0x is now in status %s\n", nodeNum, getStatusString().c_str());
}

int NaraEntry::processNextStep() {
  if (status == COOLDOWN) {
    if(millis() - lastInteraction > 60000) setStatus(UNCONTACTED);
    return 0;
  }

  if (status == UNCONTACTED || status == GAME_INVITE_RECEIVED) {
    return startGame();
  }

  if (status == GAME_ACCEPTED || status == GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US) {
    return playGameTurn();
  }

  if((status == GAME_INVITE_SENT || gameJustEnded()) && millis() - lastInteraction < 30000) {
    // if we just sent an invite or finished a game, we linger a lil longer on this naraEntry
    return 1; // consider "an action done", so we don't move to the next NaraEntry
  }

  return checkDeadlines();
}

int NaraEntry::checkDeadlines() {
  uint32_t now = millis();
  bool isGameDrawOrAbandoned = (status == GAME_DRAW || status == GAME_ABANDONED);
  bool isGameWonOrLost = (status == GAME_WON || status == GAME_LOST);

  // don't spam logs
  if(now - lastInteraction <= GAME_GHOST_TTL) {
    LOG_DEBUG("node %0x is in status %s\n", nodeNum, getStatusString().c_str());
  }

  if ((isGameDrawOrAbandoned && now - lastInteraction > DRAW_RETRY_TIME_MS) || (isGameWonOrLost && now - lastInteraction > PLAY_EVERY_MS)) {
    if(lowPowerMode()) {
      // LOG_DEBUG("NARA skipping retry game with %0x for now, we are in low power mode\n", nodeNum);
      return 0;
    }
    setStatus(UNCONTACTED);
    setLog("will challenge soon!");
    return random(5 * 1000, 20 * 1000);
  } else if(isGameInProgress() && now - lastInteraction > GAME_GHOST_TTL) {
    setStatus(GAME_ABANDONED);
    return 1;
  } else if(status == GAME_INVITE_SENT && now - lastInteraction > REINVITE_AFTER) {
    LOG_INFO("NARA node %0x is in status %s for too long, abandoning\n", nodeNum, getStatusString().c_str());
    setStatus(UNCONTACTED);
    return 0;
  }

  return 0;
}

int NaraEntry::startGame() {
  if(naraModule->gamesInProgress() > 0) { // only play one game at a time
    LOG_DEBUG("NARA skipping game with %0x for now, we are already playing another game\n", nodeNum);
    return 0;
  }

  if(lowPowerMode() && gameCount > 0) {
    // LOG_DEBUG("NARA skipping new game with %0x for now, we are in low power mode\n", nodeNum);
    return 0;
  }

  snprintf(ourText, sizeof(ourText), "%d", random(100, 999));
  ourText[31] = '\0';

  if(status == UNCONTACTED) {
    sendGameInvite(nodeNum, ourText);
    setStatus(GAME_INVITE_SENT);
  } else {
    sendGameAccept(nodeNum, ourText);
    setStatus(GAME_ACCEPTED);
  }

  return 1;
}

int NaraEntry::playGameTurn() {
  NodeNum localNodeNum = nodeDB->getNodeNum();

  char haikuText[128];

  if(localNodeNum < nodeNum) {
    snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", ourText, theirText, localNodeNum);
  } else {
    snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", theirText, ourText, localNodeNum);
  }

  ourSignature = crypto->performHashcash(haikuText, NUM_ZEROES, lastSignatureCounter, HASH_TURN_SIZE);
  // if we couldn't find a hash in HASH_TURN_SIZE iterations, we'll try again next time
  if(ourSignature == 0 && lastSignatureCounter <= MAX_HASHCASH) {
    lastSignatureCounter += HASH_TURN_SIZE;
    lastInteraction = millis();
    return 100; // yield thread for 100ms
  }

  sendGameMove(nodeNum, haikuText, ourSignature);

  if(status == GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US) {
    setStatus(GAME_CHECKING_WHO_WON);
    checkWhoWon();
    resetGame();
  }else{
    setStatus(GAME_WAITING_FOR_OPPONENT_TURN);
  }

  return 1;
}

void NaraEntry::setLog(String log)
{
  LOG_INFO("%s\n", log.c_str());
  screenLog = log;
  //naraModule->setLog(log);
}

bool NaraEntry::sendGameInvite(NodeNum dest, char* haikuText) {
  return naraModule->sendHaiku(dest, haikuText, meshtastic_NaraMessage_MessageType_GAME_INVITE, 0);
}

bool NaraEntry::sendGameAccept(NodeNum dest, char* haikuText) {
  return naraModule->sendHaiku(dest, haikuText, meshtastic_NaraMessage_MessageType_GAME_ACCEPT, 0);
}

bool NaraEntry::sendGameMove(NodeNum dest, char* haikuText, int signature) {
  return naraModule->sendHaiku(dest, haikuText, meshtastic_NaraMessage_MessageType_GAME_TURN, signature);
}

String NaraEntry::nodeName() {
  String otherNodeName = nodeDB->getMeshNode(nodeNum)->user.short_name;
  if(otherNodeName == "") {
    otherNodeName = String(nodeNum, HEX);
  }
  return otherNodeName;
}

bool NaraEntry::lowPowerMode() {
  if(powerStatus->getHasUSB()) return false;
  if(powerStatus->getBatteryChargePercent() > 50) return false;

  return true;
}

