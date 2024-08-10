#include "NaraEntry.h"
#include "NaraModule.h"
#include "ProtobufModule.h"
#include "PowerStatus.h"
#include "mesh/generated/meshtastic/nara.pb.h"
#include "main.h" // for screen

#define NUM_ZEROES 4                    // for hashcash
#define HASH_TURN_SIZE 1000             // size of hashcash turn before yielding thread
#define MAX_HASHCASH 100000             // give up game after these many iterations
#define GAME_GHOST_TTL 60000           // assume ghosted after 60 seconds
#define DRAW_RETRY_TIME_MS 5000            // retry in 5 seconds in case of draw
#define PLAY_EVERY_MS 2 * 60 * 1000        // play new game every 2 minutes
#define REINVITE_AFTER 10 * 60 * 1000      // reinvite after 10 minutes

bool NaraEntry::handleMeshPacket(const meshtastic_MeshPacket& mp, meshtastic_NaraMessage* nm) {
  if(nm->type == meshtastic_NaraMessage_MessageType_GAME_INVITE || nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT) {
    return acceptGame(nm);
  } else if(nm->type == meshtastic_NaraMessage_MessageType_GAME_TURN) {
    processOtherTurn(nm);
    return true;
  } else if(nm->type == meshtastic_NaraMessage_MessageType_HELLO) {
    return processHello(mp, nm);
  } else {
    LOG_WARN("NARA Received unexpected message from 0x%0x, type=%d. We're in status=%s\n", nodeNum, nm->type, getStatusString().c_str());
    if(isGameInProgress()) {
      abandonWeirdGame();
      return true;
    }
  }
  return false;
}

bool NaraEntry::acceptGame(meshtastic_NaraMessage* nm) {
  // if we're not expecting a new game, something went wrong
  if(isGameInProgress() || (nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT && status != GAME_INVITE_SENT)) {
    LOG_WARN("NARA won't accept game from 0x%0x. Message type %d. We're in status=%s\n", nodeNum, nm->type, getStatusString().c_str());
    abandonWeirdGame();
    return false;
  }

  if(status == GAME_INVITE_SENT && nm->type == meshtastic_NaraMessage_MessageType_GAME_INVITE) {
    LOG_INFO("NARA Received game invite from 0x%0x, but we had already invited them. Throwing away our game and playing theirs...\n", nodeNum);
    resetGame();
  }

  strncpy(theirText, nm->haiku.text, sizeof(theirText) - 1);
  theirText[sizeof(theirText) - 1] = '\0';

  if(nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT && status == GAME_INVITE_SENT) {
    setStatus(GAME_ACCEPTED);
  } else {
    setStatus(GAME_INVITE_RECEIVED);
  }
  return true;
}

void NaraEntry::processOtherTurn(meshtastic_NaraMessage* nm) {
    theirSignature = nm->haiku.signature;

    // TODO: check if their text is the same as our text (minus node num)
    // we could be in either GAME_ACCEPTED or GAME_WAITING_FOR_OPPONENT_TURN
    // we only switch status if we haven't sent our turn yet
    if(status == GAME_WAITING_FOR_OPPONENT_TURN || status == GAME_ABANDONED) {
      setStatus(GAME_CHECKING_WHO_WON);
      checkWhoWon();
      resetGame();
    } else if(status == GAME_ACCEPTED) {
      LOG_INFO("NARA Received game turn from 0x%0x before we sent ours. Will switch on next iteration.\n", nodeNum);
      setStatus(GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US);
    } else if (status == GAME_INVITE_SENT) {
      if (fastForwardGameFromTurn(nm->haiku.text)) {
        LOG_INFO("NARA Fast-forward game turn from 0x%0x after missing the game accept. Will switch on next iteration.\n", nodeNum);
        setStatus(GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US);
      } else {
        LOG_WARN("NARA Couldn't Fast-forward game turn from 0x%0x after missing the game accept.\n", nodeNum);
        abandonWeirdGame();
      }
    } else if(isGameInProgress()) {
      LOG_WARN("NARA Received game turn from 0x%0x, but we're not waiting for it. We're in status=%s\n", nodeNum, getStatusString().c_str());
      abandonWeirdGame();
    }
}

bool NaraEntry::fastForwardGameFromTurn(char* haikuText) {
  NodeNum localNodeNum = nodeDB->getNodeNum();
  std::string haikuTextStr = haikuText;

  size_t firstSlash = haikuTextStr.find('/');
  size_t secondSlash = haikuTextStr.find('/', firstSlash + 1);
  std::string tempTheirText;
  std::string tempOurSupposedText;

  if (localNodeNum < nodeNum) {
    tempTheirText = haikuTextStr.substr(firstSlash + 1, secondSlash - firstSlash - 1); // extract text from haiku between first and second /
    tempOurSupposedText = haikuTextStr.substr(0, firstSlash); // extract text from haiku until first /
  } else {
    tempTheirText = haikuTextStr.substr(0, firstSlash); // extract text from haiku until first /
    tempOurSupposedText = haikuTextStr.substr(firstSlash + 1, secondSlash - firstSlash - 1); // extract text from haiku between first and second /
  }
  LOG_DEBUG("NARA tempTheirText=%s, tempOurSupposedText=%s, ourText=%s\n", tempTheirText.c_str(), tempOurSupposedText.c_str(), ourText);

  // check if tempOurSupposedText is the same as ourText
  if (tempOurSupposedText == ourText) {
    strncpy(theirText, tempTheirText.c_str(), sizeof(theirText) - 1);
    theirText[sizeof(theirText) - 1] = '\0';
    strncpy(ourText, tempOurSupposedText.c_str(), sizeof(ourText) - 1);
    ourText[sizeof(ourText) - 1] = '\0';
    return true;
  } else {
    return false;
  }
}

bool NaraEntry::processHello(const meshtastic_MeshPacket& mp, meshtastic_NaraMessage* nm) {
    NodeNum localNodeNum = nodeDB->getNodeNum();
    if(mp.from == localNodeNum) return false; // ignore our own hello

    lastInteraction = millis();

    if(mp.to == NODENUM_BROADCAST) { // received request for hello, let's send what we have
      _meshtastic_NaraMessage_Stats stats = {firstSeen, lastGameTime, winCount, loseCount, drawCount};
      naraModule->sendHello(mp.from, stats);
      setLog("waved to " + nodeName());

      if(status == COOLDOWN) setStatus(REMATCH);
      if(isGameInProgress()) setStatus(GAME_ABANDONED);

      return true;
    }

    if (mp.to != localNodeNum || !wantsHello || !nm->has_stats) {
      LOG_WARN("NARA Received unrequested hello from 0x%0x.\n", mp.from);
      return false;
    }

    wantsHello = false;
    setStatsFromHello(nm);

    setLog(nodeName() + " waved back");
    if(!isGameInProgress()) setStatus(REMATCH);
    return true;
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
        setLog("sent challenge " + String(ourText) + " / ?");
      } else {
        setLog("sent challenge ? / " + String(ourText));
      }
      inviteSent = true;
      break;
    case GAME_INVITE_RECEIVED:
      if(nodeDB->getNodeNum() < nodeNum) {
        setLog("CHALLENGED!   ? / " + String(theirText));
      } else {
        setLog("CHALLENGED!   " + String(theirText) + " / ?");
      }
      break;
    case GAME_ACCEPTED:
      if(nodeDB->getNodeNum() < nodeNum) {
        setLog(String("rolled dice       ") + String(ourText) + " / " + String(theirText));
      } else {
        setLog(String("rolled dice       ") + String(theirText) + " / " + String(ourText));
      }
      break;
    case GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US:
      setLog(nodeName() + " is waiting for us");
      break;
    case GAME_WAITING_FOR_OPPONENT_TURN:
      if(ourSignature == 0) {
        setLog("couldn't find magic number");
      } else {
        if(nodeDB->getNodeNum() < nodeNum) {
          setLog("waiting for nara, " + String(ourSignature) + "/?");
        } else {
          setLog("waiting for nara, ?/" + String(ourSignature));
        }
      }
      break;
    case GAME_CHECKING_WHO_WON:
      setLog("checking who won...");
      gameCount++;
      this->lastGameTime = getValidTime(RTCQuality::RTCQualityDevice, false);
      break;
    case GAME_WON:
      if(theirSignature == 0) {
        setLog("they conceded, we win!");
      } else {
        setLog("WON!      " + String(ourSignature) + " < " + String(theirSignature));
      }
      winCount++;
      break;
    case GAME_LOST:
      if(ourSignature == 0) {
        setLog("conceding, " + nodeName() + " wins");
      } else {
        setLog("LOST!    " + String(ourSignature) + " > " + String(theirSignature));
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
    case REMATCH:
      setLog("will rematch " + nodeName() + "!");
      resetGame();
      break;
    case COOLDOWN:
      setLog(String("uh-oh! ") + String(nodeNum, HEX));
      break;
  }

  LOG_INFO("NARA node %0x is now in status %s\n", nodeNum, getStatusString().c_str());
}

int NaraEntry::processNextStep() {
  if(firstSeen == 0) firstSeen = getValidTime(RTCQuality::RTCQualityDevice, false);

  if(cooldownUntil > 0 && millis() < cooldownUntil) {
    return 0;
  }

  if (status == COOLDOWN) {
    if(millis() - lastInteraction > 60000) setStatus(UNCONTACTED);
    return 1; // block other interactions, also so title is shown
  }

  if (status == UNCONTACTED || status == GAME_INVITE_RECEIVED || status == REMATCH) {
    return startGame();
  }

  if (status == GAME_ACCEPTED || status == GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US) {
    return playGameTurn();
  }

  int deadlineResult = checkDeadlines();
  if(deadlineResult > 0) return deadlineResult;

  if((status == GAME_INVITE_SENT && interactedRecently()) || isGameInProgress() || gameJustEnded()) {
    return 1; // consider "an action done", also so title is shown
  }

  return 0;
}

int NaraEntry::checkDeadlines() {
  uint32_t now = millis();

  if(interactedRecently()) { // don't spam logs
    LOG_DEBUG("node %0x is in status %s\n", nodeNum, getStatusString().c_str());
  }

  bool shouldChangeStatusDueToInactivity =
    (status == GAME_ABANDONED && now - lastInteraction > GAME_GHOST_TTL) || // a minute after we've been ghosted, we attempt to rematch
    (status == GAME_DRAW && now - lastInteraction > DRAW_RETRY_TIME_MS && nodeDB->getNodeNum() < nodeNum) || // only the one with the lower nodeNum retries the draw
    (status == GAME_LOST && now - lastInteraction > PLAY_EVERY_MS) || // only losers ask for a rematch
    (status == GAME_INVITE_SENT && now - lastInteraction > REINVITE_AFTER);

  if (shouldChangeStatusDueToInactivity) {
    if(lowPowerMode()) return 0;
    if(status == GAME_INVITE_SENT) {
      setStatus(UNCONTACTED);
    } else {
      setStatus(REMATCH);
    }
    LOG_DEBUG("NARA node %0x is in status %s for too long, tripped time check\n", nodeNum, getStatusString().c_str());
    //addCooldownPeriod();
    return 1;
  } else if(status == GAME_WAITING_FOR_OPPONENT_TURN && now - lastInteraction > GAME_GHOST_TTL) { // only check for abandoned once we've played our turn
    attemptReSendGameMove();
    setStatus(GAME_ABANDONED);
    return 1;
  }

  return 0;
}

void NaraEntry::attemptReSendGameMove() {
  if(ourSignature == 0) return;

  LOG_INFO("NARA node %0x is in status %s, but we haven't heard from the other node in a while. Resending our move before abandoning.\n", nodeNum, getStatusString().c_str());

  char haikuText[128];
  NodeNum localNodeNum = nodeDB->getNodeNum();

  if(localNodeNum < nodeNum) {
    snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", ourText, theirText, localNodeNum);
  } else {
    snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", theirText, ourText, localNodeNum);
  }

  sendGameMove(nodeNum, haikuText, ourSignature);
}

int NaraEntry::startGame() {
  if(naraModule->gamesInProgress() > 0) { // only play one game at a time
    LOG_DEBUG("NARA skipping game with %0x for now, we are already playing another game\n", nodeNum);
    return 0;
  }

  if(lowPowerMode() && gameCount > 0) {
    setLog("skipping game, low power mode");
    return 0;
  }

  screen->setOn(true);

  snprintf(ourText, sizeof(ourText), "%d", random(100, 999));
  ourText[31] = '\0';

  if(status == GAME_INVITE_RECEIVED) {
    sendGameAccept(nodeNum, ourText);
    setStatus(GAME_ACCEPTED);
  } else {
    sendGameInvite(nodeNum, ourText);
    setStatus(GAME_INVITE_SENT);
  }

  return 1;
}

int NaraEntry::playGameTurn() {
  NodeNum localNodeNum = nodeDB->getNodeNum();

  const int SUSPENSE_FACTOR = 20000; // even if we know we're gonna lose, keep trying finding a hash for a lil longer before giving up
  char haikuText[128];

  if(localNodeNum < nodeNum) {
    snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", ourText, theirText, localNodeNum);
  } else {
    snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", theirText, ourText, localNodeNum);
  }

  // if the other oponent already found a hash, and it's better than ours, there's no point in keeping playing
  if(status == GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US && lastSignatureCounter > 0 && theirSignature != 0 && (theirSignature + SUSPENSE_FACTOR) < lastSignatureCounter) {
    LOG_INFO("NARA node %0x is in status %s, but the other node already found a better hash. Abandoning\n", nodeNum, getStatusString().c_str());
    lastSignatureCounter = MAX_HASHCASH;
  }else{
    ourSignature = crypto->performHashcash(haikuText, NUM_ZEROES, lastSignatureCounter, HASH_TURN_SIZE);
  }

  // if we couldn't find a hash in HASH_TURN_SIZE iterations, we'll try again next time
  if(ourSignature == 0 && lastSignatureCounter < MAX_HASHCASH) {
    lastSignatureCounter += HASH_TURN_SIZE;
    lastInteraction = millis();

    screenLog = String("finding number...    ") + String(lastSignatureCounter - 1);
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

