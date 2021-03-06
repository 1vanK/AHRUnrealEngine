// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/SlateWrapperTypes.h"
#include "WidgetBlueprintLibrary.generated.h"

class UDragDropOperation;
class USlateBrushAsset;

struct FPaintContext;

UCLASS(MinimalAPI)
class UWidgetBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:

	/** Creates a widget */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, meta=( WorldContext="WorldContextObject", FriendlyName="Create Widget", BlueprintInternalUseOnly="true" ), Category="Widget")
	static class UUserWidget* Create(UObject* WorldContextObject, TSubclassOf<class UUserWidget> WidgetType, APlayerController* OwningPlayer);

	/**
	 * Creates a new drag and drop operation that can be returned from a drag begin to inform the UI what i
	 * being dragged and dropped and what it looks like.
	 */
	UFUNCTION(BlueprintCallable, Category="Widget|Drag and Drop", meta=( BlueprintInternalUseOnly="true" ))
	static UDragDropOperation* CreateDragDropOperation(TSubclassOf<UDragDropOperation> OperationClass);
	
	/** Setup an input mode that allows only the UI to respond to user input. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Input")
	static void SetInputMode_UIOnly(APlayerController* Target, UWidget* InWidgetToFocus = nullptr, bool bLockMouseToViewport = false);

	/** Setup an input mode that allows only the UI to respond to user input, and if the UI doesn't handle it player input / player controller gets a chance. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Input")
	static void SetInputMode_GameAndUI(APlayerController* Target, UWidget* InWidgetToFocus = nullptr, bool bLockMouseToViewport = false, bool bHideCursorDuringCapture = true);

	/** Setup an input mode that allows only player input / player controller to respond to user input. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Input")
	static void SetInputMode_GameOnly(APlayerController* Target);

	/** */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Focus")
	static void SetFocusToGameViewport();

	/** Draws a box */
	UFUNCTION(BlueprintCallable, Category="Painting")
	static void DrawBox(UPARAM(ref) FPaintContext& Context, FVector2D Position, FVector2D Size, USlateBrushAsset* Brush, FLinearColor Tint = FLinearColor::White);

	/**
	 * Draws a line.
	 *
	 * @param PositionA		Starting position of the line in local space.
	 * @param PositionB		Ending position of the line in local space.
	 * @param Thickness				How many pixels thick this line should be.
	 * @param Tint			Color to render the line.
	 */
	UFUNCTION(BlueprintCallable, meta=( AdvancedDisplay = "5" ), Category="Painting" )
	static void DrawLine(UPARAM(ref) FPaintContext& Context, FVector2D PositionA, FVector2D PositionB, float Thickness = 1.0f, FLinearColor Tint = FLinearColor::White, bool bAntiAlias = true);

	// TODO UMG DrawLines

	/** 
	 * Draws text.
	 *
	 * @param InString		The string to draw.
	 * @param Position		The starting position where the text is drawn in local space.
	 * @param Tint			Color to render the line.
	 */
	UFUNCTION(BlueprintCallable, Category="Painting")
	static void DrawText(UPARAM(ref) FPaintContext& Context, const FString& InString, FVector2D Position, FLinearColor Tint = FLinearColor::White);

	/** The default event reply when simply handling an event. */
	UFUNCTION(BlueprintPure, Category="Widget|Event Reply")
	static FEventReply Handled();

	/** The event reply to use when you choose not to handle an event. */
	UFUNCTION(BlueprintPure, Category="Widget|Event Reply")
	static FEventReply Unhandled();

	/**  */
	UFUNCTION(BlueprintPure, meta=( HidePin="CapturingWidget", DefaultToSelf="CapturingWidget" ), Category="Widget|Event Reply")
	static FEventReply CaptureMouse(UPARAM(ref) FEventReply& Reply, UWidget* CapturingWidget);

	/**  */
	UFUNCTION(BlueprintPure, Category="Widget|Event Reply")
	static FEventReply ReleaseMouseCapture(UPARAM(ref) FEventReply& Reply);

	/**  */
	UFUNCTION(BlueprintPure, meta= (HidePin="CapturingWidget", DefaultToSelf="CapturingWidget"), Category="Widget|Event Reply")
	static FEventReply SetUserFocus(UPARAM(ref) FEventReply& Reply, UWidget* FocusWidget, bool bInAllUsers = false);

	UFUNCTION(BlueprintPure, meta = (DeprecatedFunction, DeprecationMessage = "Use SetUserFocus() instead"), Category = "Widget|Event Reply")
	static FEventReply CaptureJoystick(UPARAM(ref) FEventReply& Reply, UWidget* CapturingWidget, bool bInAllJoysticks = false);

	/**  */
	UFUNCTION(BlueprintPure, meta = (HidePin = "CapturingWidget", DefaultToSelf = "CapturingWidget"), Category = "Widget|Event Reply")
	static FEventReply ClearUserFocus(UPARAM(ref) FEventReply& Reply, bool bInAllUsers = false);

	UFUNCTION(BlueprintPure, meta = (DeprecatedFunction, DeprecationMessage = "Use ClearUserFocus() instead"), Category = "Widget|Event Reply")
	static FEventReply ReleaseJoystickCapture(UPARAM(ref) FEventReply& Reply, bool bInAllJoysticks = false);

	/**  */
	UFUNCTION(BlueprintPure, Category="Widget|Event Reply")
	static FEventReply SetMousePosition(UPARAM(ref) FEventReply& Reply, FVector2D NewMousePosition);

	/**
	 * Ask Slate to detect if a user started dragging in this widget.
	 * If a drag is detected, Slate will send an OnDragDetected event.
	 *
	 * @param WidgetDetectingDrag  Detect dragging in this widget
	 * @param DragKey		       This button should be pressed to detect the drag
	 */
	UFUNCTION(BlueprintPure, meta=( HidePin="WidgetDetectingDrag", DefaultToSelf="WidgetDetectingDrag" ), Category="Widget|Drag and Drop|Event Reply")
	static FEventReply DetectDrag(UPARAM(ref) FEventReply& Reply, UWidget* WidgetDetectingDrag, FKey DragKey);

	UFUNCTION(BlueprintCallable, meta=( HidePin="WidgetDetectingDrag", DefaultToSelf="WidgetDetectingDrag" ), Category="Widget|Drag and Drop|Event Reply")
	static FEventReply DetectDragIfPressed(const FPointerEvent& PointerEvent, UWidget* WidgetDetectingDrag, FKey DragKey);

	/**
	 * An event should return FReply::Handled().EndDragDrop() to request that the current drag/drop operation be terminated.
	 */
	UFUNCTION(BlueprintPure, Category="Widget|Drag and Drop|Event Reply")
	static FEventReply EndDragDrop(UPARAM(ref) FEventReply& Reply);

	/**
	 * Returns true if a drag/drop event is occurring that a widget can handle.
	 */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category="Widget|Drag and Drop")
	static bool IsDragDropping();

	/**
	 * Returns the drag and drop operation that is currently occuring if any, otherwise nothing.
	 */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category="Widget|Drag and Drop")
	static UDragDropOperation* GetDragDroppingContent();

	/**
	 * Creates a Slate Brush from a Slate Brush Asset
	 *
	 * @return A new slate brush using the asset's brush.
	 */
	UFUNCTION(BlueprintPure, Category="Widget|Brush")
	static FSlateBrush MakeBrushFromAsset(USlateBrushAsset* BrushAsset);

	/** 
	 * Creates a Slate Brush from a Texture2D
	 *
	 * @param Width  When less than or equal to zero, the Width of the brush will default to the Width of the Texture
	 * @param Height  When less than or equal to zero, the Height of the brush will default to the Height of the Texture
	 *
	 * @return A new slate brush using the texture.
	 */
	UFUNCTION(BlueprintPure, Category="Widget|Brush")
	static FSlateBrush MakeBrushFromTexture(UTexture2D* Texture, int32 Width = 0, int32 Height = 0);

	/**
	 * Creates a Slate Brush from a Material.  Materials don't have an implicit size, so providing a widget and height
	 * is required to hint slate with how large the image wants to be by default.
	 *
	 * @return A new slate brush using the material.
	 */
	UFUNCTION(BlueprintPure, Category="Widget|Brush")
	static FSlateBrush MakeBrushFromMaterial(UMaterialInterface* Material, int32 Width = 32, int32 Height = 32);

	/**
	 * Creates a Slate Brush that wont draw anything, the "Null Brush".
	 *
	 * @return A new slate brush that wont draw anything.
	 */
	UFUNCTION(BlueprintPure, Category="Widget|Brush")
	static FSlateBrush NoResourceBrush();

	/**
	 * Gets the material that allows changes to parameters at runtime.  The brush must already have a material assigned to it, 
	 * if it does it will automatically be converted to a MID.
	 *
	 * @return A material that supports dynamic input from the game.
	 */
	UFUNCTION(BlueprintPure, Category="Widget|Brush")
	static UMaterialInstanceDynamic* GetDynamicMaterial(FSlateBrush& Brush);

	/** Closes any popup menu */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Widget|Menu")
	static void DismissAllMenus();

	/**
	 * Find all widgets of a certain class.
	 * @param FoundWidgets The widgets that were found matching the filter.
	 * @param WidgetClass The widget class to filter by.
	 * @param TopLevelOnly Only the widgets that are direct children of the viewport will be returned.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Widget", meta=( WorldContext="WorldContextObject" ))
	static void GetAllWidgetsOfClass(UObject* WorldContextObject, TArray<UUserWidget*>& FoundWidgets, TSubclassOf<UUserWidget> WidgetClass, bool TopLevelOnly = true);
};
