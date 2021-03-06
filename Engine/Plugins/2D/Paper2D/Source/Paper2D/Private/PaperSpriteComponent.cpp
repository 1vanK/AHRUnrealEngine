// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "Paper2DPrivatePCH.h"
#include "PaperSpriteSceneProxy.h"
#include "PaperSpriteComponent.h"
#include "PaperCustomVersion.h"
#include "Runtime/Engine/Classes/PhysicsEngine/BodySetup2D.h"
#include "Runtime/Engine/Public/ContentStreaming.h"
#include "Runtime/Core/Public/Logging/MessageLog.h"
#include "Runtime/Core/Public/Misc/MapErrors.h"
#include "Runtime/CoreUObject/Public/Misc/UObjectToken.h"

#define LOCTEXT_NAMESPACE "Paper2D"

//////////////////////////////////////////////////////////////////////////
// UPaperSpriteComponent

UPaperSpriteComponent::UPaperSpriteComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	MaterialOverride_DEPRECATED = nullptr;

	SpriteColor = FLinearColor::White;

	CastShadow = false;
	bUseAsOccluder = false;
}

#if WITH_EDITOR
void UPaperSpriteComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FBodyInstanceEditorHelpers::EnsureConsistentMobilitySimulationSettingsOnPostEditChange(this, PropertyChangedEvent);

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#if WITH_EDITORONLY_DATA
void UPaperSpriteComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FPaperCustomVersion::GUID);
}

void UPaperSpriteComponent::PostLoad()
{
	Super::PostLoad();

	const int32 PaperVer = GetLinkerCustomVersion(FPaperCustomVersion::GUID);

	if (PaperVer < FPaperCustomVersion::ConvertPaperSpriteComponentToBeMeshComponent)
	{
		if (MaterialOverride_DEPRECATED != nullptr)
		{
			SetMaterial(0, MaterialOverride_DEPRECATED);
		}
	}
}
#endif

FPrimitiveSceneProxy* UPaperSpriteComponent::CreateSceneProxy()
{
	FPaperSpriteSceneProxy* NewProxy = new FPaperSpriteSceneProxy(this);
	if (SourceSprite != nullptr)
	{
		FSpriteDrawCallRecord DrawCall;
		DrawCall.BuildFromSprite(SourceSprite);
		DrawCall.Color = SpriteColor;
		NewProxy->SetSprite_RenderThread(DrawCall, SourceSprite->AlternateMaterialSplitIndex);
	}
	return NewProxy;
}

FBoxSphereBounds UPaperSpriteComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	if (SourceSprite != nullptr)
	{
		// Graphics bounds.
		FBoxSphereBounds NewBounds = SourceSprite->GetRenderBounds().TransformBy(LocalToWorld);

		// Add bounds of collision geometry (if present).
		if (UBodySetup* BodySetup = SourceSprite->BodySetup)
		{
			const FBox AggGeomBox = BodySetup->AggGeom.CalcAABB(LocalToWorld);
			if (AggGeomBox.IsValid)
			{
				NewBounds = Union(NewBounds,FBoxSphereBounds(AggGeomBox));
			}
		}

		// Apply bounds scale
		NewBounds.BoxExtent *= BoundsScale;
		NewBounds.SphereRadius *= BoundsScale;

		return NewBounds;
	}
	else
	{
		return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.f);
	}
}

void UPaperSpriteComponent::SendRenderDynamicData_Concurrent()
{
	if (SceneProxy != nullptr)
	{
		FSpriteDrawCallRecord DrawCall;
		DrawCall.BuildFromSprite(SourceSprite);
		DrawCall.Color = SpriteColor;
		int32 SplitIndex = (SourceSprite != nullptr) ? SourceSprite->AlternateMaterialSplitIndex : INDEX_NONE;

		ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
				FSendPaperSpriteComponentDynamicData,
				FPaperSpriteSceneProxy*,InSceneProxy,(FPaperSpriteSceneProxy*)SceneProxy,
				FSpriteDrawCallRecord,InSpriteToSend,DrawCall,
				int32,InSplitIndex,SplitIndex,
			{
				InSceneProxy->SetSprite_RenderThread(InSpriteToSend, InSplitIndex);
			});
	}
}

bool UPaperSpriteComponent::HasAnySockets() const
{
	if (SourceSprite != nullptr)
	{
		return SourceSprite->HasAnySockets();
	}

	return false;
}

bool UPaperSpriteComponent::DoesSocketExist(FName InSocketName) const
{
	if (SourceSprite != nullptr)
	{
		if (FPaperSpriteSocket* Socket = SourceSprite->FindSocket(InSocketName))
		{
			return true;
		}
	}

	return false;
}

FTransform UPaperSpriteComponent::GetSocketTransform(FName InSocketName, ERelativeTransformSpace TransformSpace) const
{
	if (SourceSprite != nullptr)
	{
		if (FPaperSpriteSocket* Socket = SourceSprite->FindSocket(InSocketName))
		{
			FTransform SocketLocalTransform = Socket->LocalTransform;
			SocketLocalTransform.ScaleTranslation(SourceSprite->GetUnrealUnitsPerPixel());

			switch (TransformSpace)
			{
				case RTS_World:
					return SocketLocalTransform * ComponentToWorld;

				case RTS_Actor:
					if (const AActor* Actor = GetOwner())
					{
						const FTransform SocketTransform = SocketLocalTransform * ComponentToWorld;
						return SocketTransform.GetRelativeTransform(Actor->GetTransform());
					}
					break;

				case RTS_Component:
					return SocketLocalTransform;

				default:
					check(false);
			}
		}
	}

	return Super::GetSocketTransform(InSocketName, TransformSpace);
}

void UPaperSpriteComponent::QuerySupportedSockets(TArray<FComponentSocketDescription>& OutSockets) const
{
	if (SourceSprite != nullptr)
	{
		return SourceSprite->QuerySupportedSockets(OutSockets);
	}
}

UBodySetup* UPaperSpriteComponent::GetBodySetup()
{
	return (SourceSprite != nullptr) ? SourceSprite->BodySetup : nullptr;
}

bool UPaperSpriteComponent::SetSprite(class UPaperSprite* NewSprite)
{
	if (NewSprite != SourceSprite)
	{
		// Don't allow changing the sprite if we are "static".
		AActor* Owner = GetOwner();
		if (!IsRegistered() || (Owner == nullptr) || (Mobility != EComponentMobility::Static))
		{
			SourceSprite = NewSprite;

			// Need to send this to render thread at some point
			MarkRenderStateDirty();

			// Update physics representation right away
			RecreatePhysicsState();

			// Notify the streaming system. Don't use Update(), because this may be the first time the mesh has been set
			// and the component may have to be added to the streaming system for the first time.
			IStreamingManager::Get().NotifyPrimitiveAttached(this, DPT_Spawned);

			// Since we have new mesh, we need to update bounds
			UpdateBounds();

			return true;
		}
	}

	return false;
}

void UPaperSpriteComponent::GetUsedTextures(TArray<UTexture*>& OutTextures, EMaterialQualityLevel::Type QualityLevel)
{
	// Get any textures referenced by our materials
	Super::GetUsedTextures(OutTextures, QualityLevel);

	// Get the texture referenced by the sprite
	if (SourceSprite != nullptr)
	{
		if (UTexture* BakedTexture = SourceSprite->GetBakedTexture())
		{
			OutTextures.AddUnique(BakedTexture);
		}
	}
}

UMaterialInterface* UPaperSpriteComponent::GetMaterial(int32 MaterialIndex) const
{
	if (OverrideMaterials.IsValidIndex(MaterialIndex) && (OverrideMaterials[MaterialIndex] != nullptr))
	{
		return OverrideMaterials[MaterialIndex];
	}
	else if (SourceSprite != nullptr)
	{
		return SourceSprite->GetMaterial(MaterialIndex);
	}

	return nullptr;
}

void UPaperSpriteComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials) const
{
	return Super::GetUsedMaterials(OutMaterials);
}

void UPaperSpriteComponent::GetStreamingTextureInfo(TArray<FStreamingTexturePrimitiveInfo>& OutStreamingTextures) const
{
	//@TODO: PAPER2D: Need to support this for proper texture streaming
	return Super::GetStreamingTextureInfo(OutStreamingTextures);
}

int32 UPaperSpriteComponent::GetNumMaterials() const
{
	if (SourceSprite != nullptr)
	{
		return FMath::Max<int32>(OverrideMaterials.Num(), SourceSprite->GetNumMaterials());
	}
	else
	{
		return FMath::Max<int32>(OverrideMaterials.Num(), 1);
	}
}

UPaperSprite* UPaperSpriteComponent::GetSprite()
{
	return SourceSprite;
}

void UPaperSpriteComponent::SetSpriteColor(FLinearColor NewColor)
{
	// Can't set color on a static component
	if (!(IsRegistered() && (Mobility == EComponentMobility::Static)) && (SpriteColor != NewColor))
	{
		SpriteColor = NewColor;

		//@TODO: Should we send immediately?
		MarkRenderDynamicDataDirty();
	}
}

FLinearColor UPaperSpriteComponent::GetWireframeColor() const
{
	if (Mobility == EComponentMobility::Static)
	{
		return FColor(0, 255, 255, 255);
	}
	else
	{
		if (BodyInstance.bSimulatePhysics)
		{
			return FColor(0, 255, 128, 255);
		}
		else
		{
			return FColor(255, 0, 255, 255);
		}
	}
}

const UObject* UPaperSpriteComponent::AdditionalStatObject() const
{
	return SourceSprite;
}

#if WITH_EDITOR
void UPaperSpriteComponent::CheckForErrors()
{
	Super::CheckForErrors();

	AActor* Owner = GetOwner();

	for (int32 MaterialIndex = 0; MaterialIndex < GetNumMaterials(); ++MaterialIndex)
	{
		if (UMaterialInterface* Material = GetMaterial(MaterialIndex))
		{
			if (!Material->IsTwoSided())
			{
				FMessageLog("MapCheck").Warning()
					->AddToken(FUObjectToken::Create(Owner))
					->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_PaperSpriteMaterialNotTwoSided", "The material applied to the sprite component is not marked as two-sided, which may cause lighting artifacts.")))
					->AddToken(FUObjectToken::Create(Material))
					->AddToken(FMapErrorToken::Create(FName(TEXT("PaperSpriteMaterialNotTwoSided"))));
			}
		}
	}
}
#endif

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE