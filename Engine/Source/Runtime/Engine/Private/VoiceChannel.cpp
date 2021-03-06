// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VoiceChannel.cpp: Unreal voice traffic implementation.
=============================================================================*/

#include "EnginePrivate.h"
#include "Net/UnrealNetwork.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemTypes.h"
#include "Net/NetworkProfiler.h"
#include "Engine/VoiceChannel.h"

/** Cleans up any voice data remaining in the queue */
bool UVoiceChannel::CleanUp( const bool bForDestroy )
{
	// Clear out refs to any voice packets so we don't leak
	VoicePackets.Empty();
	// Route to the parent class for their cleanup
	return Super::CleanUp( bForDestroy );
}

/**
 * Processes the in bound bunch to extract the voice data
 *
 * @param Bunch the voice data to process
 */
void UVoiceChannel::ReceivedBunch(FInBunch& Bunch)
{
	if (Connection->Driver && Connection->Driver->World)
	{
		IOnlineVoicePtr VoiceInt = Online::GetVoiceInterface(Connection->Driver->World);
		if (VoiceInt.IsValid())
		{
			while (!Bunch.AtEnd())
			{
				// Give the data to the local voice processing
				TSharedPtr<FVoicePacket> VoicePacket = VoiceInt->SerializeRemotePacket(Bunch);
				if (VoicePacket.IsValid())
				{
					if (Connection->Driver->ServerConnection == NULL)
					{
						// Possibly replicate the data to other clients
						Connection->Driver->ReplicateVoicePacket(VoicePacket, Connection);
					}
#if STATS
					// Increment the number of voice packets we've received
					Connection->Driver->VoicePacketsRecv++;
					Connection->Driver->VoiceBytesRecv += VoicePacket->GetBufferSize();
#endif
				}
			}
		}
	}
}

/**
 * Performs any per tick update of the VoIP state
 */
void UVoiceChannel::Tick()
{
	// If the handshaking hasn't completed throw away all unreliable voice data
	if (Connection->PlayerController &&
		Connection->PlayerController->MuteList.bHasVoiceHandshakeCompleted)
	{
		// Try to append each packet in turn
		int32 Index;
		for (Index = 0; Index < VoicePackets.Num(); Index++)
		{
			if (Connection->IsNetReady(0) == false)
			{
				// If the network is saturated bail early
				UE_LOG(LogNet, Warning, TEXT("Network saturated"));
				break;
			}

			FOutBunch Bunch(this, 0);

			TSharedPtr<FVoicePacket> Packet = VoicePackets[Index];

			// First send must be reliable as must any packet marked reliable
			Bunch.bReliable = OpenAcked == false || Packet->IsReliable();

			// Append the packet data (copies into the bunch)
			Packet->Serialize(Bunch);

#if STATS
			// Increment the number of voice packets we've sent
			Connection->Driver->VoicePacketsSent++;
			Connection->Driver->VoiceBytesSent += Packet->GetBufferSize();
#endif
			// Don't submit the bunch if something went wrong
			if (Bunch.IsError() == false)
			{
				// Submit the bunching with merging on
				SendBunch(&Bunch, 1);
			}
			else
			{
				//bail and try again next frame
				UE_LOG(LogNet, Warning, TEXT("Bunch error"));
				break;
			}
		}

		if(Index >= VoicePackets.Num())
		{
			// all sent, can throw everything away
			VoicePackets.Empty();
		}
		else if(Index > 0)
		{
			// didn't send everything, just remove all packets actually sent
			VoicePackets.RemoveAt(0,Index);
		}
	}

	// make sure we keep any reliable messages to try again next time
	// but ditch any unreliable messages we've not managed to send
	int PacketLoss = 0;
	for(int i=VoicePackets.Num() - 1; i >= 0; i--)
	{
		if(!VoicePackets[i]->IsReliable())
		{
			VoicePackets.RemoveAt(i,1,false);
			PacketLoss++;
		}
	}
	if(PacketLoss > 0)
	{
		UE_LOG(LogNet, Warning, TEXT("Dropped %d packets due to congestion in the voicechannel"), PacketLoss);
	}
}

/**
 * Adds the voice packet to the list to send for this channel
 *
 * @param VoicePacket the voice packet to send
 */
void UVoiceChannel::AddVoicePacket(TSharedPtr<FVoicePacket> VoicePacket)
{
	if (VoicePacket.IsValid())
	{
		VoicePackets.Add(VoicePacket);

		UE_LOG(LogNet, VeryVerbose, TEXT("AddVoicePacket: %s [%s] to=%s from=%s"),
			*Connection->PlayerId->ToDebugString(),
			*Connection->Driver->GetDescription(),
			*Connection->LowLevelDescribe(),
			*VoicePacket->GetSender()->ToDebugString());
	}
}