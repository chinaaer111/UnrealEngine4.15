// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "ProjectPackagingSettings.generated.h"

/**
 * Enumerates the available build configurations for project packaging.
 */
UENUM()
enum EProjectPackagingBuildConfigurations
{
	/** Debug configuration. */
	PPBC_DebugGame UMETA(DisplayName="DebugGame"),

	/** Development configuration. */
	PPBC_Development UMETA(DisplayName="Development"),

	/** Shipping configuration. */
	PPBC_Shipping UMETA(DisplayName="Shipping")
};

/**
 * Enumerates the available internationalization data presets for project packaging.
 */
UENUM()
enum class EProjectPackagingInternationalizationPresets : uint8
{
	/** English only. */
	English,

	/** English, French, Italian, German, Spanish. */
	EFIGS,

	/** English, French, Italian, German, Spanish, Chinese, Japanese, Korean. */
	EFIGSCJK,

	/** Chinese, Japanese, Korean. */
	CJK,

	/** All known cultures. */
	All
};

/**
* Enumerates the available methods for Blueprint nativization during project packaging.
*/
UENUM()
enum class EProjectPackagingBlueprintNativizationMethod : uint8
{
	/** Disable Blueprint nativization (default). */
	Disabled,

	/** Enable nativization for all Blueprint assets. */
	Inclusive,

	/** Enable nativization for selected Blueprint assets only. */
	Exclusive
};

/**
 * Implements the Editor's user settings.
 */
UCLASS(config=Game, defaultconfig)
class UNREALED_API UProjectPackagingSettings
	: public UObject
{
	GENERATED_UCLASS_BODY()

public:

	/** The build configuration for which the project is packaged. */
	UPROPERTY(config, EditAnywhere, Category=Project)
	TEnumAsByte<EProjectPackagingBuildConfigurations> BuildConfiguration;

	/** The directory to which the packaged project will be copied. */
	UPROPERTY(config, EditAnywhere, Category=Project)
	FDirectoryPath StagingDirectory;

	/**
	 * If enabled, a full rebuild will be enforced each time the project is being packaged.
	 * If disabled, only modified files will be built, which can improve iteration time.
	 * Unless you iterate on packaging, we recommend full rebuilds when packaging.
	 */
	UPROPERTY(config, EditAnywhere, Category=Project)
	bool FullRebuild;

	/**
	 * If enabled, a distribution build will be created and the shipping configuration will be used
	 * If disabled, a development build will be created
	 * Distribution builds are for publishing to the App Store
	 */
	UPROPERTY(config, EditAnywhere, Category=Project)
	bool ForDistribution;

	/** If enabled, debug files will be included in the packaged game */
	UPROPERTY(config, EditAnywhere, Category=Project)
	bool IncludeDebugFiles;

	/** If enabled, then the project's Blueprint assets (including structs and enums) will be intermediately converted into C++ and used in the packaged project (in place of the .uasset files).*/
	UPROPERTY(config, EditAnywhere, Category = Blueprints)
	EProjectPackagingBlueprintNativizationMethod BlueprintNativizationMethod;

	/** List of Blueprints to include for nativization when using the exclusive method. */
	UPROPERTY(config, EditAnywhere, AdvancedDisplay, Category = Blueprints, meta = (DisplayName = "List of Blueprint assets to nativize", RelativeToGameContentDir, LongPackageName))
	TArray<FFilePath> NativizeBlueprintAssets;

	/** If enabled, a warning will be emitted at build/cook time if nativization is turned on in the Project Settings, but the nativization flag was omitted from the command line. */
	UPROPERTY(config, EditAnywhere, Category = Blueprints)
	bool bWarnIfPackagedWithoutNativizationFlag;

	/** If enabled, all content will be put into a single .pak file instead of many individual files (default = enabled). */
	UPROPERTY(config, EditAnywhere, Category=Packaging)
	bool UsePakFile;

	/** 
	 * If enabled, will generate pak file chunks.  Assets can be assigned to chunks in the editor or via a delegate (See ShooterGameDelegates.cpp). 
	 * Can be used for streaming installs (PS4 Playgo, XboxOne Streaming Install, etc)
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging)
	bool bGenerateChunks;

	/**
	* Normally during chunk generation all dependencies of a package in a chunk will be pulled into that package's chunk.
	* If this is enabled then only hard dependencies are pulled in. Soft dependencies stay in their original chunk.
	*/
	UPROPERTY(config, EditAnywhere, Category = Packaging)
	bool bChunkHardReferencesOnly;

	/** 
	 * If enabled, will generate data for HTTP Chunk Installer. This data can be hosted on webserver to be installed at runtime. Requires "Generate Chunks" enabled.
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging)
	bool bBuildHttpChunkInstallData;

	/** 
	 * When "Build HTTP Chunk Install Data" is enabled this is the directory where the data will be build to.
	 */	
	UPROPERTY(config, EditAnywhere, Category = Packaging)
	FDirectoryPath HttpChunkInstallDataDirectory;

	/** 
	 * Version name for HTTP Chunk Install Data.
	 */
	UPROPERTY(config, EditAnywhere, Category = Packaging)
	FString HttpChunkInstallDataVersion;

	/** Specifies whether to include prerequisites of packaged games, such as redistributable operating system components, whenever possible. */
	UPROPERTY(config, EditAnywhere, Category=Packaging)
	bool IncludePrerequisites;

	/** A directory containing prerequisite packages that should be staged in the executable directory. Can be relative to $(EngineDir) or $(ProjectDir) */
	UPROPERTY(config, EditAnywhere, Category = Packaging, AdvancedDisplay)
	FDirectoryPath ApplocalPrerequisitesDirectory;

	/**
	 * Specifies whether to include the crash reporter in the packaged project. 
	 * This is included by default for Blueprint based projects, but can optionally be disabled.
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay)
	bool IncludeCrashReporter;

	/** Predefined sets of culture whose internationalization data should be packaged. */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Internationalization Support"))
	EProjectPackagingInternationalizationPresets InternationalizationPreset;

	/** Cultures whose data should be cooked, staged, and packaged. */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Localizations to Package"))
	TArray<FString> CulturesToStage;

	/** Culture to use if no matching culture is found. */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Package default localization"))
	FString DefaultCulture;

	/**
	 * Cook all things in the project content directory
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Cook everything in the project content directory (ignore list of maps below)"))
	bool bCookAll;

	/**
	 * Cook only maps (this only affects the cookall flag)
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Cook only maps (this only affects cookall)"))
	bool bCookMapsOnly;


	/**
	 * Create compressed cooked packages (decreased deployment size)
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Create compressed cooked packages"))
	bool bCompressed;

	/**
	* Encrypt ini files inside of the pak file
	*/
	UPROPERTY(config, EditAnywhere, Category = Packaging, AdvancedDisplay, meta = (DisplayName = "Encrypt ini files inside pak files"))
	bool bEncryptIniFiles;

	
	/**
	* Don't include content in any editor folders when cooking.  This can cause issues with missing content in cooked games if the content is being used. 
	*/
	UPROPERTY(config, EditAnywhere, Category = Packaging, AdvancedDisplay, meta = (DisplayName = "Exclude editor content when cooking"))
	bool bSkipEditorContent;

	/**
	 * List of maps to include when no other map list is specified on commandline
	 */
	UPROPERTY(config, EditAnywhere, Category = Packaging, AdvancedDisplay, meta = (DisplayName = "List of maps to include in a packaged build", RelativeToGameContentDir, LongPackageName))
	TArray<FFilePath> MapsToCook;	

	/**
	 * Directories containing .uasset files that should always be cooked regardless of whether they're referenced by anything in your project
	 * Note: These paths are relative to your project Content directory
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Additional Asset Directories to Cook", RelativeToGameContentDir))
	TArray<FDirectoryPath> DirectoriesToAlwaysCook;
	

	/**
	* Directories containing .uasset files that should always be cooked regardless of whether they're referenced by anything in your project
	* Note: These paths are relative to your project Content directory
	*/
	UPROPERTY(config, EditAnywhere, Category = Packaging, AdvancedDisplay, meta = (DisplayName = "Directories to never cook", RelativeToGameContentDir))
	TArray<FDirectoryPath> DirectoriesToNeverCook;


	/**
	 * Directories containing files that should always be added to the .pak file (if using a .pak file; otherwise they're copied as individual files)
	 * This is used to stage additional files that you manually load via the UFS (Unreal File System) file IO API
	 * Note: These paths are relative to your project Content directory
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Additional Non-Asset Directories to Package", RelativeToGameContentDir))
	TArray<FDirectoryPath> DirectoriesToAlwaysStageAsUFS;

	/**
	 * Directories containing files that should always be copied when packaging your project, but are not supposed to be part of the .pak file
	 * This is used to stage additional files that you manually load without using the UFS (Unreal File System) file IO API, eg, third-party libraries that perform their own internal file IO
	 * Note: These paths are relative to your project Content directory
	 */
	UPROPERTY(config, EditAnywhere, Category=Packaging, AdvancedDisplay, meta=(DisplayName="Additional Non-Asset Directories To Copy", RelativeToGameContentDir))
	TArray<FDirectoryPath> DirectoriesToAlwaysStageAsNonUFS;	

private:
	/** Helper array used to mirror Blueprint asset selections across edits */
	TArray<FFilePath> CachedNativizeBlueprintAssets;

	UPROPERTY(config)
	bool bNativizeBlueprintAssets_DEPRECATED;

	UPROPERTY(config)
	bool bNativizeOnlySelectedBlueprints_DEPRECATED;
	
public:

	// UObject Interface

	virtual void PostInitProperties() override;
	virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent ) override;
	virtual bool CanEditChange( const UProperty* InProperty ) const override;

	/** Adds the given Blueprint asset to the exclusive nativization list. */
	bool AddBlueprintAssetToNativizationList(const class UBlueprint* InBlueprint);

	/** Removes the given Blueprint asset from the exclusive nativization list. */
	bool RemoveBlueprintAssetFromNativizationList(const class UBlueprint* InBlueprint);

	/** Determines if the specified Blueprint is already saved for exclusive nativization. */
	bool IsBlueprintAssetInNativizationList(const class UBlueprint* InBlueprint) const { return FindBlueprintInNativizationList(InBlueprint) >= 0; }

private:
	/** Returns the index of the specified Blueprint in the exclusive nativization list (otherwise INDEX_NONE) */
	int32 FindBlueprintInNativizationList(const UBlueprint* InBlueprint) const;
};
