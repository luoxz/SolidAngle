#pragma once
#include "YYUT.h"
#include <d3d11shader.h>

DECLARE_LOG_CATEGORY_EXTERN(ShaderLog, Log, All);
struct ShaderMacroEntry
{
	FString MacroName;
	FString Value;
};


class IShader
{
public:
	IShader() = default;
	IShader(const IShader&) = delete;
	IShader(IShader &&) = delete;
	IShader& operator=(const IShader&) = delete;
	virtual ~IShader() = 0;
public:
	virtual void				AddShaderMacros(const TArray<ShaderMacroEntry> & InShaderMacroEntrys) = 0;
	virtual void				SetInclude(const FString & ShaderSrcInclude)=0;
	virtual bool				AddAlias(const FString & AliasName)=0;
	virtual bool				CreateShader(const FString &FileName, const FString &MainPoint)=0;
	virtual bool				BindResource(const FString &ParaName, int32 n)=0;
	virtual bool				BindResource(const FString &ParaName, float f)=0;
	virtual bool				BindResource(const FString &ParaName, float* f, int Num)=0;
	virtual bool				BindResource(const FString &ParaName, FVector2D V2)=0;
	virtual bool				BindResource(const FString &ParaName, FVector V3)=0;
	virtual bool				BindResource(const FString &ParaName, FPlane V4)=0;
	virtual bool				BindResource(const FString &ParaName, const FMatrix  &Mat)=0;
	virtual bool				BindResource(const FString &ParaName, const FMatrix  *Mat,int Num)=0;
	virtual bool				Update()=0;
	virtual bool				BindInputLayout(TArray<D3D11_INPUT_ELEMENT_DESC> lInputLayoutDesc)=0;
protected:
	virtual TComPtr<ID3D11DeviceChild>  GetInternalResource()const = 0;
};


struct YConstantBuffer
{
	YConstantBuffer(unsigned int BufferSizeInBytes)
		:CBSize(BufferSizeInBytes)
		,ShadowBuffer(MakeUnique<unsigned char[]>(BufferSizeInBytes))
		,CBType(eCBType::Num)
		,BindSlotIndex(-1)
		,BindSlotNum(-1)
	{
		
	}
	bool AllocResource();
	enum class eCBType
	{
		Scalar,
		Texture,
		Interface,
		BindInfo,
		Num
	};
	FString CBName;
	uint32 CBSize;//in bytes;
	TUniquePtr<uint8[]> ShadowBuffer;
	eCBType CBType;
	int32 BindSlotIndex;
	int32 BindSlotNum;
	template<class T>
	struct YCBScalar
	{
		static T& GetValue(YConstantBuffer *ConstantBuffer,unsigned int Offset) 
		{
			return *((T*)(ConstantBuffer->ShadowBuffer.Get() + Offset));
		}
		static void SetValue(YConstantBuffer *ConstantBuffer,unsigned int Offset,T Value)
		{
			*((T*)(ConstantBuffer->ShadowBuffer.Get() + Offset)) = Value;
		}
	};

	template<class T,int N>
	struct YCBVector
	{
		static T* GetValue(YConstantBuffer *ConstantBuffer, unsigned int Offset) 
		{
			return (T*)(ConstantBuffer->ShadowBuffer.Get() + Offset);
		}
		static void SetValue(YConstantBuffer *ConstantBuffer, unsigned int Offset,T* Value)
		{
			memcpy_s((ConstantBuffer->ShadowBuffer.Get() + Offset), N * sizeof(T), Value, N * sizeof(T));
		}
	};
	typedef YCBVector<float, 2> YCBVector2;
	typedef YCBVector<float, 3> YCBVector3;
	typedef YCBVector<float, 4> YCBVector4;
	struct YCBMatrix4X4
	{
		static FMatrix& GetValue(YConstantBuffer *ConstantBuffer, unsigned int Offset)
		{
			return *((FMatrix*)(ConstantBuffer->ShadowBuffer.Get() + Offset));
		}
		static void SetValue(YConstantBuffer *ConstantBuffer, unsigned int Offset, const FMatrix& Value)
		{
			*((FMatrix*)(ConstantBuffer->ShadowBuffer.Get() + Offset)) = Value.GetTransposed();
		}
	};

	TComPtr<ID3D11Buffer>		D3DBuffer;
};


class IShaderBind :public IShader
{
public:
	IShaderBind() = default;
	virtual ~IShaderBind();
	virtual bool PostReflection(TComPtr<ID3DBlob> &Blob, TComPtr<ID3D11ShaderReflection>& ShaderReflector);
	virtual void				AddShaderMacros(const TArray<ShaderMacroEntry> & InShaderMacroEntrys) override;
	virtual void				SetInclude(const FString & ShaderSrcInclude) override;
	virtual bool				AddAlias(const FString & AliasName) override;
	virtual bool				Update() override;
	virtual bool				BindResource(const FString &ParaName, int32 n) override;
	virtual bool				BindResource(const FString &ParaName, float f) override;
	virtual bool				BindResource(const FString &ParaName, float* f, int Num) override;
	virtual bool				BindResource(const FString &ParaName, FVector2D V2) override;
	virtual bool				BindResource(const FString &ParaName, FVector V3) override;
	virtual bool				BindResource(const FString &ParaName, FPlane V4) override;
	virtual bool				BindResource(const FString &ParaName, const FMatrix  &Mat) override;
	virtual bool				BindResource(const FString &ParaName, const FMatrix  *Mat, int Num) override;
	virtual bool				BindInputLayout(TArray<D3D11_INPUT_ELEMENT_DESC> lInputLayoutDesc) override { return true; };
protected:
	bool						ReflectShader(TComPtr<ID3DBlob> Blob);
	FString					ShaderPath;
	FString					ShaderIncludePath;
	FString					AliasNameForDebug;
	TArray<ShaderMacroEntry> ShaderMacroEntrys;
	TArray<TUniquePtr<YConstantBuffer>> ConstantBuffers;
	struct ScalarIndex
	{
		unsigned int ConstantBufferIndex;
		unsigned int ValueIndex;
		enum class eType
		{
			BOOL,INT,FLOAT,FLOAT2,FLOAT3,FLOAT4,MATRIX4X4,NUM
		};
		// ������֤��BindResourceʱ��ShadowBuffer�������������һ�¡�
		// ��ֹ��HLSL����float g_matWorld, Matrix g_fStrength,
		// ����Cpp����BindResource(g_fStrength,1.0f),BindResource(g_matWorld,XMMATRIX::Identity())
		eType Type; 
	};

	bool						BindResourceHelp(const FString &ParaName,ScalarIndex& Index);
	void AddScalarVariable(const FString &Name, uint32 InConstantBufferIndex, uint32 InValueIndex, ScalarIndex::eType InType);
	TMap<FString, ScalarIndex> MapShaderVariableToScalar;
};

class YVSShader :public IShaderBind
{
public:
	YVSShader();
	virtual ~YVSShader();
	virtual bool				CreateShader(const FString &FileName, const FString &MainPoint) override;
	virtual bool				Update() override;
	virtual bool				BindInputLayout(TArray<D3D11_INPUT_ELEMENT_DESC> InInputLayoutDesc) override;
private:
	virtual bool PostReflection(TComPtr<ID3DBlob> &Blob, TComPtr<ID3D11ShaderReflection>& ShaderReflector) override;
	virtual TComPtr<ID3D11DeviceChild> GetInternalResource() const;
	TComPtr<ID3D11VertexShader> VertexShader;
	TComPtr<ID3D11InputLayout>  InputLayout;
	TArray<D3D11_INPUT_ELEMENT_DESC> InputLayoutDesc;
};

class YPSShader :public IShaderBind
{
public:
	YPSShader();
	virtual ~YPSShader();
	virtual bool CreateShader(const FString &FileName, const FString &MainPoint) override;
	virtual bool				Update() override;
private:
	virtual TComPtr<ID3D11DeviceChild> GetInternalResource() const;
	TComPtr<ID3D11PixelShader>  PixShader;
};
