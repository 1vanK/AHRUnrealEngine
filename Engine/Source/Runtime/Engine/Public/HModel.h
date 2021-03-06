// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HModel.h: HModel definition.
=============================================================================*/

#ifndef _INC_HMODEL
#define _INC_HMODEL

class UModelComponent;


/**
 * A hit proxy representing a UModel.
 */
class HModel : public HHitProxy
{
	DECLARE_HIT_PROXY( ENGINE_API );
public:

	/** Initialization constructor. */
	HModel(UModelComponent* InComponent, UModel* InModel):
		Component(InComponent),
		Model(InModel)
	{}

	/**
	 * Finds the surface at the given screen coordinates of a view family.
	 * @return True if a surface was found.
	 */
	ENGINE_API bool ResolveSurface(const FSceneView* View,int32 X,int32 Y,uint32& OutSurfaceIndex) const;

	// HHitProxy interface.
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override
	{
		Collector.AddReferencedObject( Component );
		Collector.AddReferencedObject( Model );
	}
	virtual EMouseCursor::Type GetMouseCursor()
	{
		return EMouseCursor::Crosshairs;
	}

	// Accessors.
	UModelComponent* GetModelComponent() const { return Component; }
	UModel* GetModel() const { return Model; }

private:
	
	UModelComponent* Component;
	UModel* Model;
};

#endif
