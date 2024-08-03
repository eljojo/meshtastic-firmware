#include "NaraEntry.h"
#include "NaraModule.h"
#include "ProtobufModule.h"

#define NUM_ZEROES 4                    // for hashcash
#define HASH_TURN_SIZE 1000             // size of hashcash turn before yielding thread
#define MAX_HASHCASH 100000             // give up game after these many iterations
#define GAME_GHOST_TTL 60000           // assume ghosted after 60 seconds
#define DRAW_RETRY_TIME_MS 5000            // retry in 5 seconds in case of draw

bool NaraEntry::sendGameInvite(NodeNum dest, char* haikuText) {
  return naraModule->sendHaiku(dest, haikuText, meshtastic_NaraMessage_MessageType_GAME_INVITE, 0);
}

bool NaraEntry::sendGameAccept(NodeNum dest, char* haikuText) {
  return naraModule->sendHaiku(dest, haikuText, meshtastic_NaraMessage_MessageType_GAME_ACCEPT, 0);
}

bool NaraEntry::sendGameMove(NodeNum dest, char* haikuText, int signature) {
  return naraModule->sendHaiku(dest, haikuText, meshtastic_NaraMessage_MessageType_GAME_TURN, signature);
}

void NaraEntry::handleMeshPacket(const meshtastic_MeshPacket& mp, meshtastic_NaraMessage* nm) {
  if(nm->type == meshtastic_NaraMessage_MessageType_GAME_INVITE) {
    resetGame();

    strncpy(theirText, nm->haiku.text, sizeof(theirText) - 1);
    theirText[sizeof(theirText) - 1] = '\0';

    setStatus(GAME_INVITE_RECEIVED);
  } else if(nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT && status == GAME_INVITE_SENT) {
    strncpy(theirText, nm->haiku.text, sizeof(theirText) - 1);
    theirText[sizeof(theirText) - 1] = '\0';

    setStatus(GAME_ACCEPTED);
  } else if(nm->type == meshtastic_NaraMessage_MessageType_GAME_TURN) {
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
      setStatus(GAME_ABANDONED);
      resetGame();
      naraModule->setLog(String("uh-oh! ") + String(nodeNum, HEX));
    }
  }else{
    LOG_WARN("NARA Received unexpected message from 0x%0x, type=%d. We're in status=%s\n", nodeNum, nm->type, getStatusString().c_str());
    if(isGameInProgress()) {
      setStatus(GAME_ABANDONED);
      resetGame();
      naraModule->setLog(String("uh-oh! ") + String(nodeNum, HEX));
    }
  }
}

void NaraEntry::setStatus(NaraEntryStatus status) {
  this->status = status;
  lastInteraction = millis();

  switch(status) {
    case UNCONTACTED:
      // naraModule->setLog("uncontacted");
      break;
    case GAME_INVITE_SENT:
      naraModule->setLog("invited " + String(nodeNum, HEX));
      break;
    case GAME_INVITE_RECEIVED:
      naraModule->setLog(String(nodeNum, HEX) + " wants to play");
      break;
    case GAME_ACCEPTED:
      if(nodeDB->getNodeNum() < nodeNum) {
        naraModule->setLog(String("thinking turn... ") + String(ourText) + "/" + String(theirText));
      } else {
        naraModule->setLog(String("thinking turn... ") + String(theirText) + "/" + String(ourText));
      }
      break;
    case GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US:
      naraModule->setLog(String(nodeNum, HEX) + " is waiting for us");
      break;
    case GAME_WAITING_FOR_OPPONENT_TURN:
      naraModule->setLog("waiting for " + String(nodeNum, HEX));
      break;
    case GAME_CHECKING_WHO_WON:
      naraModule->setLog("checking who won...");
      break;
    case GAME_WON:
      naraModule->setLog("WON game! " + String(ourSignature) + " vs " + String(theirSignature));
      break;
    case GAME_LOST:
      naraModule->setLog("lost game: " + String(ourSignature) + " vs " + String(theirSignature));
      break;
    case GAME_DRAW:
      naraModule->setLog("draw game w/" + String(nodeNum, HEX));
      break;
    case GAME_ABANDONED:
      //naraModule->setLog(String(nodeNum, HEX) + " GHOSTED us");
      break;
  }

  LOG_INFO("NARA node %0x is now in status %s\n", nodeNum, getStatusString().c_str());
}

int NaraEntry::processNextStep() {
  uint32_t now = millis();

  // don't spam logs
  if(now - lastInteraction <= GAME_GHOST_TTL) {
    LOG_INFO("node %0x is in status %s\n", nodeNum, getStatusString().c_str());
  }

  if (status == UNCONTACTED) {
    // send game invite and set haiku to random number
    snprintf(ourText, sizeof(ourText), "%d", random(10000));
    ourText[31] = '\0';

    sendGameInvite(nodeNum, ourText);
    setStatus(GAME_INVITE_SENT);

    return 1;
  } else if (status == GAME_INVITE_RECEIVED) {
    // send game invite and set haiku to random number
    snprintf(ourText, sizeof(ourText), "%d", random(10000));
    ourText[31] = '\0';

    sendGameAccept(nodeNum, ourText);
    setStatus(GAME_ACCEPTED);

    return 1;
  } else if (status == GAME_ACCEPTED || status == GAME_ACCEPTED_AND_OPPONENT_IS_WAITING_FOR_US) {
    NodeNum localNodeNum = nodeDB->getNodeNum();

    char haikuText[128];

    if(localNodeNum < nodeNum) {
      snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", ourText, theirText, localNodeNum);
    } else {
      snprintf(haikuText, sizeof(haikuText), "%s/%s/%0x", theirText, ourText, localNodeNum);
    }

    ourSignature = crypto->performHashcash(haikuText, NUM_ZEROES, lastSignatureCounter, HASH_TURN_SIZE);
    if(ourSignature == 0 && lastSignatureCounter <= MAX_HASHCASH) {
      // we couldn't find a hash in HASH_TURN_SIZE iterations, we'll try again next time
      lastSignatureCounter += HASH_TURN_SIZE;
      //naraModule->setLog("still thinking turn...");
      lastInteraction = now;
      return 100;
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
  }else if((status == GAME_DRAW && now - lastInteraction > DRAW_RETRY_TIME_MS) || (status == GAME_ABANDONED && now - lastInteraction > DRAW_RETRY_TIME_MS)) {
    setStatus(UNCONTACTED);
    naraModule->setLog("will play again w/" + String(nodeNum, HEX));
    return random(5 * 1000, 20 * 1000);
  } else if(isGameInProgress() && now - lastInteraction > GAME_GHOST_TTL) {
    setStatus(GAME_ABANDONED);
    naraModule->setLog(String(nodeNum, HEX) + " GHOSTED us");
    return 1;
  }

  return 0;
}

void NaraEntry::checkWhoWon() {
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
