// @RyanTorant
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "ApproximateHybridRaytracing.h"
#include "SceneUtils.h"
#include "SceneFilterRendering.h"
#include "AHR_Voxelization.h"

// Using a full screen quad at every stage instead of a cs as the targets are already setted for a quad. Also, not using groupshared memory.
class AHRPassVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRPassVS,Global);
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	AHRPassVS()	{}
	AHRPassVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		FGlobalShader::SetParameters(RHICmdList, GetVertexShader(),View);
	}

	// Begin FShader Interface
	virtual bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
	// End FShader Interface
private:
};
IMPLEMENT_SHADER_TYPE(,AHRPassVS,TEXT("AHRComposite"),TEXT("VS"),SF_Vertex);

void  FApproximateHybridRaytracer::InitializeViewTargets(uint32 _resX,uint32 _resY)
{	
	// New frame, new starting idx
	currentLightIDX = 0;

	if(_resX != ResX || _resY != ResY && _resX >= 128 && _resY >= 128) // If you are rendering to a target less than 128x128, you are doing it wrong. This is to bypass auxiliary views
	{
		// Store size
		ResX = _resX; ResY = _resY;

		// The view size changed, so we have to rebuild the targets
		FRHIResourceCreateInfo CreateInfo;

		// PF_FloatRGBA is 16 bits float per component. Nice documentation Epic ...
		RaytracingTarget = RHICreateTexture2D(ResX/2,ResY/2,PF_FloatRGBA,1,1,TexCreate_RenderTargetable | TexCreate_ShaderResource,CreateInfo);
		UpsampledTarget0 = RHICreateTexture2D(ResX,ResY,PF_FloatRGBA,1,1,TexCreate_RenderTargetable | TexCreate_ShaderResource,CreateInfo);
		UpsampledTarget1 = RHICreateTexture2D(ResX,ResY,PF_FloatRGBA,1,1,TexCreate_RenderTargetable | TexCreate_ShaderResource,CreateInfo);
		UpsampledTarget2 = RHICreateTexture2D(ResX,ResY,PF_FloatRGBA,1,1,TexCreate_RenderTargetable | TexCreate_ShaderResource,CreateInfo);

		RaytracingTargetSRV = RHICreateShaderResourceView(RaytracingTarget,0);
		UpsampledTargetSRV0 = RHICreateShaderResourceView(UpsampledTarget0,0);
		UpsampledTargetSRV1 = RHICreateShaderResourceView(UpsampledTarget1,0);
		UpsampledTargetSRV2 = RHICreateShaderResourceView(UpsampledTarget2,0);
	}
}

void FApproximateHybridRaytracer::VoxelizeScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRVoxelizeScene);

	// Voxelize the objects to the binary grid
	if( View.PrimitivesToVoxelize.Num( ) > 0 )
	{
		TAHRVoxelizerElementPDI<FAHRVoxelizerDrawingPolicyFactory> Drawer(
			&View, FAHRVoxelizerDrawingPolicyFactory::ContextType(RHICmdList) );

		for( int32 PrimitiveIndex = 0, Num = View.PrimitivesToVoxelize.Num( ); PrimitiveIndex < Num; PrimitiveIndex++ )
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = View.PrimitivesToVoxelize[ PrimitiveIndex ];
			
			FScopeCycleCounter Context( PrimitiveSceneInfo->Proxy->GetStatId( ) );
			Drawer.SetPrimitive( PrimitiveSceneInfo->Proxy );
			
			// Calls SceneProxy DrawDynamicElements function
			PrimitiveSceneInfo->Proxy->DrawDynamicElements(&Drawer,&View,EDrawDynamicFlags::Voxelize);
		}
	}
}

///
/// Tracing
///
BEGIN_UNIFORM_BUFFER_STRUCT(AHRTraceSceneCB,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector2D,ScreenRes)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,SliceSize)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,InvSceneBounds)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,WorldToVoxelOffset) // -SceneCenter/SceneBounds
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,invVoxel)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,InitialDispMult)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,SamplesDispMultiplier)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,GlossyRayCount)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,GlossySamplesCount)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,DiffuseRayCount)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(uint32,DiffuseSamplesCount)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,LostRayColor)
END_UNIFORM_BUFFER_STRUCT(AHRTraceSceneCB)
IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRTraceSceneCB,TEXT("AHRTraceCB"));

BEGIN_UNIFORM_BUFFER_STRUCT(AHRShadowMatrices,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix0)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix1)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix2)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix3)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FMatrix,Matrix4)

	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset0)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset1)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset2)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset3)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(FVector,Offset4)
END_UNIFORM_BUFFER_STRUCT(AHRShadowMatrices)
IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRShadowMatrices,TEXT("AHRShadowMatrices"));

class AHRTraceScenePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRTraceScenePS,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		if(CVarAHRTraceReflections.GetValueOnRenderThread() == 1)
			OutEnvironment.SetDefine(TEXT("_GLOSSY"),1);
	}

	AHRTraceScenePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		SceneVolume.Bind(Initializer.ParameterMap, TEXT("SceneVolume"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		cb.Bind(Initializer.ParameterMap, TEXT("AHRTraceCB"));
		matrixCB.Bind(Initializer.ParameterMap, TEXT("AHRShadowMatrices"));

		ShadowAlbedo0.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo0"));
		ShadowAlbedo1.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo1"));
		ShadowAlbedo2.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo2"));
		ShadowAlbedo3.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo3"));
		ShadowAlbedo4.Bind(Initializer.ParameterMap, TEXT("ShadowAlbedo4"));

		ShadowNormals0.Bind(Initializer.ParameterMap, TEXT("ShadowNormals0"));
		ShadowNormals1.Bind(Initializer.ParameterMap, TEXT("ShadowNormals1"));
		ShadowNormals2.Bind(Initializer.ParameterMap, TEXT("ShadowNormals2"));
		ShadowNormals3.Bind(Initializer.ParameterMap, TEXT("ShadowNormals3"));
		ShadowNormals4.Bind(Initializer.ParameterMap, TEXT("ShadowNormals4"));

		ShadowZ0.Bind(Initializer.ParameterMap, TEXT("ShadowZ0"));
		ShadowZ1.Bind(Initializer.ParameterMap, TEXT("ShadowZ1"));
		ShadowZ2.Bind(Initializer.ParameterMap, TEXT("ShadowZ2"));
		ShadowZ3.Bind(Initializer.ParameterMap, TEXT("ShadowZ3"));
		ShadowZ4.Bind(Initializer.ParameterMap, TEXT("ShadowZ4"));

		cmpSampler.Bind(Initializer.ParameterMap, TEXT("cmpSampler"));
	}

	AHRTraceScenePS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FShaderResourceViewRHIRef sceneVolumeSRV)
	{
		FRHIResourceCreateInfo CreateInfo;
		static auto dummyTexture = RHICreateTexture2D(1,1,PF_ShadowDepth,1,1,TexCreate_ShaderResource,CreateInfo);

		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		AHRTraceSceneCB cbdata;

		cbdata.SliceSize = CVarAHRVoxelSliceSize.GetValueOnRenderThread();
		cbdata.ScreenRes.X = View.Family->FamilySizeX/2;
		cbdata.ScreenRes.Y = View.Family->FamilySizeY/2;
		cbdata.invVoxel = FVector(1.0f / float(cbdata.SliceSize));

		cbdata.InvSceneBounds = FVector(1.0f) / View.FinalPostProcessSettings.AHRSceneScale;
		cbdata.WorldToVoxelOffset = -FVector(View.FinalPostProcessSettings.AHRSceneCenterX,View.FinalPostProcessSettings.AHRSceneCenterY,View.FinalPostProcessSettings.AHRSceneCenterZ)*cbdata.InvSceneBounds; // -SceneCenter/SceneBounds
		cbdata.GlossyRayCount = View.FinalPostProcessSettings.AHRGlossyRayCount;
		cbdata.GlossySamplesCount = View.FinalPostProcessSettings.AHRGlossySamplesCount;
		cbdata.DiffuseRayCount = View.FinalPostProcessSettings.AHRDiffuseRayCount;
		cbdata.DiffuseSamplesCount = View.FinalPostProcessSettings.AHRDiffuseSamplesCount;
		cbdata.LostRayColor.X = View.FinalPostProcessSettings.AHRLostRayColor.R;
		cbdata.LostRayColor.Y = View.FinalPostProcessSettings.AHRLostRayColor.G;
		cbdata.LostRayColor.Z = View.FinalPostProcessSettings.AHRLostRayColor.B;
		cbdata.InitialDispMult = View.FinalPostProcessSettings.AHRInitialDisplacement;
		cbdata.SamplesDispMultiplier = View.FinalPostProcessSettings.AHRSamplesDisplacement;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,cb,cbdata);

		AHRShadowMatrices matrix_cbdata;

		auto lList = AHREngine.GetLightsList();
		matrix_cbdata.Matrix0 = lList[0].ViewProj;
		matrix_cbdata.Matrix1 = lList[1].ViewProj;
		matrix_cbdata.Matrix2 = lList[2].ViewProj;
		matrix_cbdata.Matrix3 = lList[3].ViewProj;
		matrix_cbdata.Matrix4 = lList[4].ViewProj;

		matrix_cbdata.Offset0 = lList[0].Offset;
		matrix_cbdata.Offset1 = lList[1].Offset;
		matrix_cbdata.Offset2 = lList[2].Offset;
		matrix_cbdata.Offset3 = lList[3].Offset;
		matrix_cbdata.Offset4 = lList[4].Offset;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,matrixCB,matrix_cbdata);

		if(SceneVolume.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,SceneVolume.GetBaseIndex(),sceneVolumeSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());
		if(cmpSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,cmpSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap,0,0,0,SCF_Less>::GetRHI());

		FSamplerStateRHIParamRef SamplerStateLinear  = TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();

		if(ShadowAlbedo0.IsBound() && lList[0].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo0, LinearSampler, SamplerStateLinear,  lList[0].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo0, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo1.IsBound() && lList[1].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo1, LinearSampler, SamplerStateLinear,  lList[1].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo1, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo2.IsBound() && lList[2].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo2, LinearSampler, SamplerStateLinear,  lList[2].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo2, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo3.IsBound() && lList[3].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo3, LinearSampler, SamplerStateLinear,  lList[3].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo3, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowAlbedo4.IsBound() && lList[4].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo4, LinearSampler, SamplerStateLinear,  lList[4].Albedo );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowAlbedo4, LinearSampler, SamplerStateLinear, dummyTexture);

		if(ShadowNormals0.IsBound() && lList[0].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals0, LinearSampler, SamplerStateLinear,  lList[0].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals0, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals1.IsBound() && lList[1].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals1, LinearSampler, SamplerStateLinear,  lList[1].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals1, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals2.IsBound() && lList[2].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals2, LinearSampler, SamplerStateLinear,  lList[2].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals2, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals3.IsBound() && lList[3].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals3, LinearSampler, SamplerStateLinear,  lList[3].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals3, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowNormals4.IsBound() && lList[4].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals4, LinearSampler, SamplerStateLinear,  lList[4].Normals );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowNormals4, LinearSampler, SamplerStateLinear, dummyTexture);

		if(ShadowZ0.IsBound() && lList[0].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ0, LinearSampler, SamplerStateLinear,  lList[0].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ0, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ1.IsBound() && lList[1].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ1, LinearSampler, SamplerStateLinear,  lList[1].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ1, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ2.IsBound() && lList[2].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ2, LinearSampler, SamplerStateLinear,  lList[2].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ2, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ3.IsBound() && lList[3].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ3, LinearSampler, SamplerStateLinear,  lList[3].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ3, LinearSampler, SamplerStateLinear, dummyTexture);
		if(ShadowZ4.IsBound() && lList[4].IsValid)
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ4, LinearSampler, SamplerStateLinear,  lList[4].Depth );
		else
			SetTextureParameter(RHICmdList, ShaderRHI, ShadowZ4, LinearSampler, SamplerStateLinear, dummyTexture);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << SceneVolume;
		Ar << LinearSampler;
		Ar << cmpSampler;
		Ar << cb;
		Ar << matrixCB;

		Ar << ShadowAlbedo0;
		Ar << ShadowAlbedo1;
		Ar << ShadowAlbedo2;
		Ar << ShadowAlbedo3;
		Ar << ShadowAlbedo4;

		Ar << ShadowNormals0;
		Ar << ShadowNormals1;
		Ar << ShadowNormals2;
		Ar << ShadowNormals3;
		Ar << ShadowNormals4;

		Ar << ShadowZ0;
		Ar << ShadowZ1;
		Ar << ShadowZ2;
		Ar << ShadowZ3;
		Ar << ShadowZ4;
		return bShaderHasOutdatedParameters;
	}

	FGlobalBoundShaderState& GetBoundShaderState()
	{
		static FGlobalBoundShaderState State;

		return State;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter SceneVolume;
	FShaderResourceParameter LinearSampler;
	FShaderResourceParameter cmpSampler;
	TShaderUniformBufferParameter<AHRTraceSceneCB> cb;
	TShaderUniformBufferParameter<AHRShadowMatrices> matrixCB;

	FShaderResourceParameter ShadowAlbedo0;
	FShaderResourceParameter ShadowAlbedo1;
	FShaderResourceParameter ShadowAlbedo2;
	FShaderResourceParameter ShadowAlbedo3;
	FShaderResourceParameter ShadowAlbedo4;

	FShaderResourceParameter ShadowNormals0;
	FShaderResourceParameter ShadowNormals1;
	FShaderResourceParameter ShadowNormals2;
	FShaderResourceParameter ShadowNormals3;
	FShaderResourceParameter ShadowNormals4;

	FShaderResourceParameter ShadowZ0;
	FShaderResourceParameter ShadowZ1;
	FShaderResourceParameter ShadowZ2;
	FShaderResourceParameter ShadowZ3;
	FShaderResourceParameter ShadowZ4;
};
IMPLEMENT_SHADER_TYPE(,AHRTraceScenePS,TEXT("AHRTraceScene"),TEXT("PS"),SF_Pixel);

void FApproximateHybridRaytracer::TraceScene(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRTraceScene);

	// Draw a full screen quad into the half res target
	// Trace the GI and reflections if they are enabled

	// Set the viewport, raster state , depth stencil and render target
	SetRenderTarget(RHICmdList, RaytracingTarget, FTextureRHIRef());
	FIntRect SrcRect = View.ViewRect;
	FIntRect DestRect = SrcRect/2;
	RHICmdList.SetViewport(SrcRect.Min.X, SrcRect.Min.Y, 0.0f,DestRect.Max.X, DestRect.Max.Y, 1.0f);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRTraceScenePS> PixelShader(View.ShaderMap);

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, SceneVolume->SRV);

	// Draw a quad mapping scene color to the view's render target
	DrawRectangle( 
		RHICmdList,
		0, 0,
		DestRect.Width(), DestRect.Height(),
		SrcRect.Min.X, SrcRect.Min.Y, 
		SrcRect.Width(), SrcRect.Height(),
		DestRect.Size(),
		GSceneRenderTargets.GetBufferSizeXY(),
		*VertexShader,
		EDRF_UseTriangleOptimization);
}


///
/// Upsampling and composite
///
class AHRUpsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRUpsamplePS,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
		if(CVarAHRTraceReflections.GetValueOnRenderThread() == 1)
			OutEnvironment.SetDefine(TEXT("_GLOSSY"),1);
	}

	AHRUpsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		GIBufferTexture.Bind(Initializer.ParameterMap, TEXT("tGI"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		BlurKernelSize.Bind(Initializer.ParameterMap,TEXT("size"));
	}

	AHRUpsamplePS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FShaderResourceViewRHIRef giSRV,float blurKernelSize)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		if(GIBufferTexture.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,GIBufferTexture.GetBaseIndex(),giSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());

		SetShaderValue(RHICmdList, ShaderRHI, BlurKernelSize,blurKernelSize);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << GIBufferTexture;
		Ar << LinearSampler;
		Ar << BlurKernelSize;
		return bShaderHasOutdatedParameters;
	}

	FGlobalBoundShaderState& GetBoundShaderState()
	{
		static FGlobalBoundShaderState State;

		return State;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter GIBufferTexture;
	FShaderResourceParameter LinearSampler;
	FShaderParameter BlurKernelSize;
};
IMPLEMENT_SHADER_TYPE(,AHRUpsamplePS,TEXT("AHRUpsample"),TEXT("PS"),SF_Pixel);

void FApproximateHybridRaytracer::Upsample(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRUpsample);

	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRUpsamplePS> PixelShader(View.ShaderMap);

	///////// Pass 0

	// Set the viewport, raster state , depth stencil and render target
	SetRenderTarget(RHICmdList, UpsampledTarget0, FTextureRHIRef());
	
	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, RaytracingTargetSRV,1.7);

	// Draw!
	DrawRectangle( 
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y, 
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				GSceneRenderTargets.GetBufferSizeXY(),
				*VertexShader,
				EDRF_UseTriangleOptimization);

	///////// Pass 1

	// Set the viewport, raster state , depth stencil and render target
	SetRenderTarget(RHICmdList, UpsampledTarget1, FTextureRHIRef());
	
	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, UpsampledTargetSRV0,2.8);

	// Draw!
	DrawRectangle( 
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y, 
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				GSceneRenderTargets.GetBufferSizeXY(),
				*VertexShader,
				EDRF_UseTriangleOptimization);

	///////// Pass 2

	// Set the viewport, raster state , depth stencil and render target
	SetRenderTarget(RHICmdList, UpsampledTarget2, FTextureRHIRef());
	
	// Clear the target before drawing
	RHICmdList.Clear(true, FLinearColor::Black, false, 1.0f, false, 0, FIntRect());

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, UpsampledTargetSRV1,1.85);

	// Draw!
	DrawRectangle( 
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y, 
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				GSceneRenderTargets.GetBufferSizeXY(),
				*VertexShader,
				EDRF_UseTriangleOptimization);
}

BEGIN_UNIFORM_BUFFER_STRUCT(AHRCompositeCB,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER(float,GIMultiplier)
END_UNIFORM_BUFFER_STRUCT(AHRCompositeCB)
IMPLEMENT_UNIFORM_BUFFER_STRUCT(AHRCompositeCB,TEXT("AHRCompositeCB"));

class AHRCompositePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(AHRCompositePS,Global)
public:

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Platform, OutEnvironment);
	}

	AHRCompositePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		DeferredParameters.Bind(Initializer.ParameterMap);
		GIBufferTexture.Bind(Initializer.ParameterMap, TEXT("tGI"));
		LinearSampler.Bind(Initializer.ParameterMap, TEXT("samLinear"));
		cb.Bind(Initializer.ParameterMap,TEXT("AHRCompositeCB"));
	}

	AHRCompositePS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FShaderResourceViewRHIRef giSRV)
	{
		const FPixelShaderRHIParamRef ShaderRHI = GetPixelShader();
		FGlobalShader::SetParameters(RHICmdList, ShaderRHI,View);
		DeferredParameters.Set(RHICmdList, ShaderRHI, View);

		if(GIBufferTexture.IsBound())
			RHICmdList.SetShaderResourceViewParameter(ShaderRHI,GIBufferTexture.GetBaseIndex(),giSRV);
		if(LinearSampler.IsBound())
			RHICmdList.SetShaderSampler(ShaderRHI,LinearSampler.GetBaseIndex(),TStaticSamplerState<SF_Trilinear,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI());

		AHRCompositeCB cbdata;

		cbdata.GIMultiplier = View.FinalPostProcessSettings.AHRIntensity;

		SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI,cb,cbdata);
	}

	virtual bool Serialize(FArchive& Ar)
	{		
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << DeferredParameters;
		Ar << GIBufferTexture;
		Ar << LinearSampler;
		Ar << cb;
		return bShaderHasOutdatedParameters;
	}

	FGlobalBoundShaderState& GetBoundShaderState()
	{
		static FGlobalBoundShaderState State;
		
		return State;
	}

private:
	FDeferredPixelShaderParameters DeferredParameters;
	FShaderResourceParameter GIBufferTexture;
	FShaderResourceParameter LinearSampler;
	TShaderUniformBufferParameter<AHRCompositeCB> cb;
};
IMPLEMENT_SHADER_TYPE(,AHRCompositePS,TEXT("AHRComposite"),TEXT("PS"),SF_Pixel);

void FApproximateHybridRaytracer::Composite(FRHICommandListImmediate& RHICmdList,FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList,AHRComposite);

	// Simply render a full screen quad and sample the upsampled buffer. Use additive blending to mix it with the light accumulation buffer
	// Only one view at a time for now (1/11/2014)

	// Set additive blending
	RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());
	//RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI());

	// add gi and multiply scene color by ao
	// final = gi + ao*direct
	//RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_One, BF_One>::GetRHI());


	//		DEBUG!!!!!
	//RHICmdList.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_Zero, BF_SourceColor, BO_Add, BF_One, BF_One>::GetRHI());



	// Set the viewport, raster state and depth stencil
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
	RHICmdList.SetRasterizerState(TStaticRasterizerState<FM_Solid, CM_None>::GetRHI());
	RHICmdList.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	// Get the shaders
	TShaderMapRef<AHRPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<AHRCompositePS> PixelShader(View.ShaderMap);

	// Bound shader parameters
	SetGlobalBoundShaderState(RHICmdList, View.FeatureLevel, PixelShader->GetBoundShaderState(),  GFilterVertexDeclaration.VertexDeclarationRHI, *VertexShader, *PixelShader);
	VertexShader->SetParameters(RHICmdList,View);
	PixelShader->SetParameters(RHICmdList, View, UpsampledTargetSRV2); // just binds the upsampled texture using SetTextureParameter()

	// Draw!
	DrawRectangle( 
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y, 
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				GSceneRenderTargets.GetBufferSizeXY(),
				*VertexShader,
				EDRF_UseTriangleOptimization);
}