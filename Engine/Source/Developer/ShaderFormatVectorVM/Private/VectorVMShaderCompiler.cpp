// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
// ..

#include "ShaderFormatVectorVM.h"
#include "VectorVMBackend.h"
#include "CoreMinimal.h"
#include "FileHelper.h"
#include "ShaderCore.h"
#include "CrossCompilerCommon.h"
#include "ShaderCompilerCommon.h"
#include "ShaderPreprocessor.h"
#include "INiagaraCompiler.h"

#include "VectorVM.h"

DEFINE_LOG_CATEGORY_STATIC(LogVectorVMShaderCompiler, Log, All); 


/*------------------------------------------------------------------------------
	External interface.
------------------------------------------------------------------------------*/

static FString CreateCrossCompilerBatchFile( const FString& ShaderFile, const FString& OutputFile, const FString& EntryPoint, EHlslShaderFrequency Frequency, uint8 Version, uint32 CCFlags ) 
{
	const TCHAR* VersionSwitch = TEXT("");
	return CrossCompiler::CreateBatchFileContents(ShaderFile, OutputFile, Frequency, EntryPoint, VersionSwitch, CCFlags, TEXT(""));
}

/**
 * Compile a shader for the VectorVM on Windows.
 * @param Input - The input shader code and environment.
 * @param Output - Contains shader compilation results upon return.
 */
void CompileShader_VectorVM(const FShaderCompilerInput& Input, FShaderCompilerOutput& Output, const FString& WorkingDirectory, uint8 Version)
{

}

//TODO: Move to this output living in the shader eco-system with the compute shaders too but for now just do things more directly.
void CompileShader_VectorVM(const FShaderCompilerInput& Input, FShaderCompilerOutput& Output,const FString& WorkingDirectory, uint8 Version, FNiagaraCompilationOutput& NiagaraOutput)
{
	FString PreprocessedShader;
	FShaderCompilerDefinitions AdditionalDefines;
	EHlslCompileTarget HlslCompilerTarget = HCT_FeatureLevelSM5;
	ECompilerFlags PlatformFlowControl = CFLAG_AvoidFlowControl;

	AdditionalDefines.SetDefine(TEXT("COMPILER_HLSLCC"), 1);
	AdditionalDefines.SetDefine(TEXT("COMPILER_VECTORVM"), 1);

	const bool bDumpDebugInfo = (Input.DumpDebugInfoPath != TEXT("") && IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath));

	AdditionalDefines.SetDefine(TEXT("FORCE_FLOATS"), (uint32)1);

	auto DoPreprocess = [&]() -> bool
	{
		if (Input.bSkipPreprocessedCache)
		{
			return FFileHelper::LoadFileToString(PreprocessedShader, *Input.SourceFilename);
		}
		else
		{
			return PreprocessShader(PreprocessedShader, Output, Input, AdditionalDefines);
		}
	};

	if (DoPreprocess())
	{
		//TODO: Need to remove any unsupported features here?

		char* ShaderSource = NULL;
		char* ErrorLog = NULL;

		const EHlslShaderFrequency Frequency = HSF_VertexShader;

		//TODO: Do this later when we implement the rest of the shader plumbing stuff.
// 		// Write out the preprocessed file and a batch file to compile it if requested (DumpDebugInfoPath is valid)
// 		if (bDumpDebugInfo)
// 		{
// 			FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / Input.SourceFilename + TEXT(".usf")));
// 			if (FileWriter)
// 			{
// 				auto AnsiSourceFile = StringCast<ANSICHAR>(*PreprocessedShader);
// 				FileWriter->Serialize((ANSICHAR*)AnsiSourceFile.Get(), AnsiSourceFile.Length());
// 				FileWriter->Close();
// 				delete FileWriter;
// 			}
// 
// 			if (Input.bGenerateDirectCompileFile)
// 			{
// 				FFileHelper::SaveStringToFile(CreateShaderCompilerWorkerDirectCommandLine(Input), *(Input.DumpDebugInfoPath / TEXT("DirectCompile.txt")));
// 			}
// 		}

		//Is stuff like this needed? What others?
		uint32 CCFlags = HLSLCC_NoPreprocess;
		CCFlags |= HLSLCC_PrintAST;
		//CCFlags |= HLSLCC_UseFullPrecisionInPS;

		//TODO: Do this later when we implement the rest of the shader plumbing stuff.
// 		if (bDumpDebugInfo)
// 		{
// 			const FString VVMFile = (Input.DumpDebugInfoPath / TEXT("Output.vvm"));
// 			const FString USFFile = (Input.DumpDebugInfoPath / Input.SourceFilename) + TEXT(".usf");
// 			const FString CCBatchFileContents = CreateCrossCompilerBatchFile(USFFile, VVMFile, *Input.EntryPointName, Frequency, Version, CCFlags);
// 			if (!CCBatchFileContents.IsEmpty())
// 			{
// 				const TCHAR * ScriptName = PLATFORM_WINDOWS ? TEXT("CrossCompile.bat") : TEXT("CrossCompile.sh");
// 				FFileHelper::SaveStringToFile(CCBatchFileContents, *(Input.DumpDebugInfoPath / ScriptName));
// 			}
// 		}

		//NEEDED?
		// Required as we added the RemoveUniformBuffersFromSource() function (the cross-compiler won't be able to interpret comments w/o a preprocessor)
		//CCFlags &= ~HLSLCC_NoPreprocess;

		FVectorVMCodeBackend VVMBackEnd(CCFlags, HlslCompilerTarget, NiagaraOutput);
		FVectorVMLanguageSpec VVMLanguageSpec; 

		int32 Result = 0;
		FHlslCrossCompilerContext CrossCompilerContext(CCFlags, Frequency, HlslCompilerTarget);
		if (CrossCompilerContext.Init(TCHAR_TO_ANSI(*Input.SourceFilename), &VVMLanguageSpec))
		{
			Result = CrossCompilerContext.Run(
				TCHAR_TO_ANSI(*PreprocessedShader),
				TCHAR_TO_ANSI(*Input.EntryPointName),
				&VVMBackEnd,
				&ShaderSource,
				&ErrorLog
				) ? 1 : 0;
		}

		NiagaraOutput.Errors = ErrorLog;

		//TODO: Try to get rid of the CompilationOutput and have the vm bytecode life in the shader eco-system as the compute shader version will.
//		int32 SourceLen = VVMBackEnd.ByteCode.Num();//ShaderSource ? FCStringAnsi::Strlen(ShaderSource) : 0;
// 		if (SourceLen > 0)
// 		{
// 			if (bDumpDebugInfo)
// 			{
// 				const FString GLSLFile = (Input.DumpDebugInfoPath / TEXT("Output.vvm"));
// 
// 				if (SourceLen > 0)
// 				{
// 					uint32 Len = FCStringAnsi::Strlen(TCHAR_TO_ANSI(*Input.SourceFilename)) + FCStringAnsi::Strlen(TCHAR_TO_ANSI(*Input.EntryPointName)) + FCStringAnsi::Strlen(ShaderSource) + 20;
// 					char* Dest = (char*)malloc(Len);
// 					FCStringAnsi::Snprintf(Dest, Len, "// ! %s.usf:%s\n%s", (const char*)TCHAR_TO_ANSI(*Input.SourceFilename), (const char*)TCHAR_TO_ANSI(*Input.EntryPointName), (const char*)ShaderSource);
// 					free(ShaderSource);
// 					ShaderSource = Dest;
// 					SourceLen = FCStringAnsi::Strlen(ShaderSource);
// 					
// 					FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*(Input.DumpDebugInfoPath / Input.SourceFilename + TEXT(".vvm")));
// 					if (FileWriter)
// 					{
// 						FileWriter->Serialize(ShaderSource,SourceLen+1);
// 						FileWriter->Close();
// 						delete FileWriter;
// 					}
// 				}
// 			}

			//HMMMM....?
// #if VALIDATE_GLSL_WITH_DRIVER
// 			PrecompileShader(Output, Input, ShaderSource, Version, Frequency);
// #else // VALIDATE_GLSL_WITH_DRIVER
			//SourceLen = FCStringAnsi::Strlen(ShaderSource);
//			Output.Target = Input.Target;
			//BuildShaderOutput(Output, Input, ShaderSource, SourceLen, Version);

//			Output.bSucceeded = true;
			//Should we add optional data to define the register allocations etc so that someone can just write hlsl directly and have it become a niagara sim?
//			Output.ShaderCode.GetWriteAccess().Append((uint8*)VVMBackEnd.ByteCode.GetData(), VVMBackEnd.ByteCode.Num());
//			Output.ShaderCode.FinalizeShaderCode();
//#endif // VALIDATE_GLSL_WITH_DRIVER
// 		}
// 		else
// 		{
			//ES2 Command line is throwing me off. Is this needed?

// 			if (bDumpDebugInfo)
// 			{
// 				// Generate the batch file to help track down cross-compiler issues if necessary
// 				const FString VVMFile = (Input.DumpDebugInfoPath / TEXT("Output.vvm"));
// 				const FString VVMBatchFileContents = CreateCommandLineGLSLES2(VVMFile, (Input.DumpDebugInfoPath / TEXT("Output.asm")), Version, Frequency, false);
// 				if (!VVMBatchFileContents.IsEmpty())
// 				{
// 					FFileHelper::SaveStringToFile(VVMBatchFileContents, *(Input.DumpDebugInfoPath / TEXT("VVMCompile.bat")));
// 				}
// 			}
// 
// 			Output.bSucceeded = false;
// 
// 			FString Tmp = ANSI_TO_TCHAR(ErrorLog);
// 			TArray<FString> ErrorLines;
// 			Tmp.ParseIntoArray(ErrorLines, TEXT("\n"), true);
// 			for (int32 LineIndex = 0; LineIndex < ErrorLines.Num(); ++LineIndex)
// 			{
// 				const FString& Line = ErrorLines[LineIndex];
// 				CrossCompiler::ParseHlslccError(Output.Errors, Line);
// 			}
// 		}

		if (ShaderSource)
		{
			UE_LOG(LogVectorVMShaderCompiler, Warning, TEXT("%s"), (const char*)ShaderSource);
			free(ShaderSource);
		}
		if (ErrorLog)
		{
			UE_LOG(LogVectorVMShaderCompiler, Warning, TEXT("%s"), (const char*)ErrorLog);
			free(ErrorLog);
		}
	}
}
