#include "NaraEntry.h"
#include "NaraModule.h"
#include "ProtobufModule.h"

#define NUM_ZEROES 4                  // for hashcash

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

    status = GAME_INVITE_RECEIVED;

    LOG_INFO("NARA Received game invite from %0x\n", nodeNum);
    naraModule->setLog(String(nodeNum) + " wants to play");
  } else if(nm->type == meshtastic_NaraMessage_MessageType_GAME_ACCEPT && status == GAME_INVITE_SENT) {
    strncpy(theirText, nm->haiku.text, sizeof(theirText) - 1);
    theirText[sizeof(theirText) - 1] = '\0';

    status = GAME_ACCEPTED;

    LOG_INFO("NARA Received game accept from %0x\n", nodeNum);
    naraModule->setLog(String(nodeNum) + " accepted");
  } else if(nm->type == meshtastic_NaraMessage_MessageType_GAME_TURN) {
    // check who won, dependin on our status (waiting of game accepted)
    theirSignature = nm->haiku.signature;
    if(status == GAME_WAITING_FOR_OPPONENT_TURN) {
      checkWhoWon();
    }else{
      LOG_WARN("NARA Received game turn from %0x, but we're not waiting for it. We're in status=%s\n", nodeNum, getStatusString());
    }
  }else{
    LOG_WARN("NARA Received unexpected message from %0x, type=%d\n", nodeNum, nm->type);
  }
}

bool NaraEntry::processNextStep() {
  if (status == UNCONTACTED) {
    // send game invite and set haiku to random number
    snprintf(ourText, sizeof(ourText), "%d", random(10000));
    ourText[31] = '\0';

    sendGameInvite(nodeNum, ourText);
    status = GAME_INVITE_SENT;

    LOG_INFO("NARA Sent game invite to %0x\n", nodeNum);
    naraModule->setLog("inviting " + String(nodeNum, HEX));

    return true;
  } else if (status == GAME_INVITE_RECEIVED) {
    // send game invite and set haiku to random number
    snprintf(ourText, sizeof(ourText), "%d", random(10000));
    ourText[31] = '\0';

    sendGameAccept(nodeNum, ourText);
    status = GAME_ACCEPTED;

    LOG_INFO("NARA Sent game accept to %0x\n", nodeNum);
    naraModule->setLog("playing w/" + String(nodeNum, HEX));

    return true;
  } else if (status == GAME_ACCEPTED) {
    NodeNum localNodeNum = nodeDB->getNodeNum();

    char haikuText[128];

    // both nodes add the pieces in consistent order, and add their own name
    if(localNodeNum < nodeNum) {
      snprintf(haikuText, sizeof(haikuText), "%s%s%s", ourText, theirText, owner.short_name);
    } else {
      snprintf(haikuText, sizeof(haikuText), "%s%s%s", theirText, ourText, owner.short_name);
    }

    ourSignature = crypto->performHashcash(haikuText, NUM_ZEROES);
    sendGameMove(nodeNum, haikuText, ourSignature);

    if(theirSignature > 0) {
      checkWhoWon();
    }else{
      status = GAME_WAITING_FOR_OPPONENT_TURN;
      naraModule->setLog("waiting for " + String(nodeNum, HEX));
      LOG_INFO("NARA Waiting for game turn from %0x\n", nodeNum);
    }
    return true;
  }
  return false;
}

void NaraEntry::checkWhoWon() {
  if(status == GAME_WAITING_FOR_OPPONENT_TURN || status == GAME_ACCEPTED) {
    if(ourSignature >= theirSignature) {
      status = GAME_WON;

      naraModule->setLog("won game w/" + String(nodeNum, HEX));
      LOG_INFO("NARA Won game against %0x, our_signature=%d, their_signature=%d\n", nodeNum, ourSignature, theirSignature);
    } else if(ourSignature < theirSignature) {
      status = GAME_LOST;
      naraModule->setLog("lost game w/" + String(nodeNum, HEX));
      LOG_INFO("NARA Lost game against %0x, our_signature=%d, their_signature=%d\n", nodeNum, ourSignature, theirSignature);
    }
  }else{
    status = GAME_ABANDONED;
    naraModule->setLog("abandoned game w/" + String(nodeNum, HEX));
    LOG_WARN("NARA Abandoned game against %0x\n", nodeNum);
  }
}
