// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/SolidAngleString.h"
#include "HAL/FileManager.h"
#include "Containers/ArrayView.h"

/*-----------------------------------------------------------------------------
FFileHelper
-----------------------------------------------------------------------------*/
struct CORE_API FFileHelper
{
	struct EHashOptions
	{
		enum Type
		{
			/** Enable the async task for verifying the hash for the file being loaded */
			EnableVerify = 1 << 0,
			/** A missing hash entry should trigger an error */
			ErrorMissingHash = 1 << 1
		};
	};

	struct EEncodingOptions
	{
		enum Type
		{
			AutoDetect,
			ForceAnsi,
			ForceUnicode,
			ForceUTF8,
			ForceUTF8WithoutBOM
		};
	};

	/**
	* Load a text file to an YString.
	* Supports all combination of ANSI/Unicode files and platforms.
	*/
	static void BufferToString(YString& Result, const uint8* Buffer, int32 Size);

	/**
	* Load a binary file to a dynamic array.
	*/
	static bool LoadFileToArray(TArray<uint8>& Result, const TCHAR* Filename, uint32 Flags = 0);

	/**
	* Load a text file to an YString.
	* Supports all combination of ANSI/Unicode files and platforms.
	* @param Result string representation of the loaded file
	* @param Filename name of the file to load
	* @param VerifyFlags flags controlling the hash verification behavior ( see EHashOptions )
	*/
	static bool LoadFileToString(YString& Result, const TCHAR* Filename, uint32 VerifyFlags = 0);

	/**
	* Save a binary array to a file.
	*/
	static bool SaveArrayToFile(TArrayView<const uint8> Array, const TCHAR* Filename, IFileManager* FileManager = &IFileManager::Get(), uint32 WriteFlags = 0);

	/**
	* Write the YString to a file.
	* Supports all combination of ANSI/Unicode files and platforms.
	*/
	static bool SaveStringToFile(const YString& String, const TCHAR* Filename, EEncodingOptions::Type EncodingOptions = EEncodingOptions::AutoDetect, IFileManager* FileManager = &IFileManager::Get(), uint32 WriteFlags = 0);

	/**
	* Saves a 24/32Bit BMP file to disk
	*
	* @param Pattern filename with path, must not be 0, if with "bmp" extension (e.g. "out.bmp") the filename stays like this, if without (e.g. "out") automatic index numbers are addended (e.g. "out00002.bmp")
	* @param DataWidth - Width of the bitmap supplied in Data >0
	* @param DataHeight - Height of the bitmap supplied in Data >0
	* @param Data must not be 0
	* @param SubRectangle optional, specifies a sub-rectangle of the source image to save out. If NULL, the whole bitmap is saved
	* @param FileManager must not be 0
	* @param OutFilename optional, if specified filename will be output
	* @param bInWriteAlpha opional, specifies whether to write out the alpha channel. Will force BMP V4 format.
	*
	* @return true if success
	*/
	static bool CreateBitmap(const TCHAR* Pattern, int32 DataWidth, int32 DataHeight, const struct YColor* Data, struct YIntRect* SubRectangle = NULL, IFileManager* FileManager = &IFileManager::Get(), YString* OutFilename = NULL, bool bInWriteAlpha = false);

	/**
	* Generates the next unique bitmap filename with a specified extension
	*
	* @param Pattern		Filename with path, but without extension.
	* @oaran Extension		File extension to be appended
	* @param OutFilename	Reference to an YString where the newly generated filename will be placed
	* @param FileManager	Reference to a IFileManager (or the global instance by default)
	*
	* @return true if success
	*/
	static bool GenerateNextBitmapFilename(const YString& Pattern, const YString& Extension, YString& OutFilename, IFileManager* FileManager = &IFileManager::Get());

	/**
	*	Load the given ANSI text file to an array of strings - one YString per line of the file.
	*	Intended for use in simple text parsing actions
	*
	*	@param	InFilename			The text file to read, full path
	*	@param	InFileManager		The filemanager to use - NULL will use &IFileManager::Get()
	*	@param	OutStrings			The array of YStrings to fill in
	*
	*	@return	bool				true if successful, false if not
	*/
	static bool LoadANSITextFileToStrings(const TCHAR* InFilename, IFileManager* InFileManager, TArray<YString>& OutStrings);
};
