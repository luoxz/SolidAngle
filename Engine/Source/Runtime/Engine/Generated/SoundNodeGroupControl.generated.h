// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	C++ class header boilerplate exported from UnrealHeaderTool.
	This is automatically generated by the tools.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#ifdef ENGINE_SoundNodeGroupControl_generated_h
#error "SoundNodeGroupControl.generated.h already included, missing '#pragma once' in SoundNodeGroupControl.h"
#endif
#define ENGINE_SoundNodeGroupControl_generated_h

#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_RPC_WRAPPERS
#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_RPC_WRAPPERS_NO_PURE_DECLS
#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_INCLASS_NO_PURE_DECLS \
	private: \
	static void StaticRegisterNativesUSoundNodeGroupControl(); \
	friend ENGINE_API class UClass* Z_Construct_UClass_USoundNodeGroupControl(); \
	public: \
	DECLARE_CLASS(USoundNodeGroupControl, USoundNode, COMPILED_IN_FLAGS(0), 0, TEXT("/Script/Engine"), ENGINE_API) \
	DECLARE_SERIALIZER(USoundNodeGroupControl) \
	/** Indicates whether the class is compiled into the engine */ \
	enum {IsIntrinsic=COMPILED_IN_INTRINSIC};


#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_INCLASS \
	private: \
	static void StaticRegisterNativesUSoundNodeGroupControl(); \
	friend ENGINE_API class UClass* Z_Construct_UClass_USoundNodeGroupControl(); \
	public: \
	DECLARE_CLASS(USoundNodeGroupControl, USoundNode, COMPILED_IN_FLAGS(0), 0, TEXT("/Script/Engine"), ENGINE_API) \
	DECLARE_SERIALIZER(USoundNodeGroupControl) \
	/** Indicates whether the class is compiled into the engine */ \
	enum {IsIntrinsic=COMPILED_IN_INTRINSIC};


#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_STANDARD_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	ENGINE_API USoundNodeGroupControl(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(USoundNodeGroupControl) \
	DECLARE_VTABLE_PTR_HELPER_CTOR(ENGINE_API, USoundNodeGroupControl); \
DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(USoundNodeGroupControl); \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	ENGINE_API USoundNodeGroupControl(USoundNodeGroupControl&&); \
	ENGINE_API USoundNodeGroupControl(const USoundNodeGroupControl&); \
public:


#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_ENHANCED_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	ENGINE_API USoundNodeGroupControl(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()) : Super(ObjectInitializer) { }; \
private: \
	/** Private move- and copy-constructors, should never be used */ \
	ENGINE_API USoundNodeGroupControl(USoundNodeGroupControl&&); \
	ENGINE_API USoundNodeGroupControl(const USoundNodeGroupControl&); \
public: \
	DECLARE_VTABLE_PTR_HELPER_CTOR(ENGINE_API, USoundNodeGroupControl); \
DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(USoundNodeGroupControl); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(USoundNodeGroupControl)


#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_PRIVATE_PROPERTY_OFFSET
#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_19_PROLOG
#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_GENERATED_BODY_LEGACY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_PRIVATE_PROPERTY_OFFSET \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_RPC_WRAPPERS \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_INCLASS \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_STANDARD_CONSTRUCTORS \
public: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


#define Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_PRIVATE_PROPERTY_OFFSET \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_RPC_WRAPPERS_NO_PURE_DECLS \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_INCLASS_NO_PURE_DECLS \
	Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h_22_ENHANCED_CONSTRUCTORS \
static_assert(false, "Unknown access specifier for GENERATED_BODY() macro in class SoundNodeGroupControl."); \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID Engine_Source_Runtime_Engine_Classes_Sound_SoundNodeGroupControl_h


PRAGMA_ENABLE_DEPRECATION_WARNINGS