// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeOutput.h"
#include "UObject/UnrealType.h"
#include "INiagaraCompiler.h"
#include "NiagaraScript.h"

#include "EdGraphSchema_Niagara.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeOutput"

UNiagaraNodeOutput::UNiagaraNodeOutput(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UNiagaraNodeOutput::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
// 	for (FNiagaraVariable& Output : Outputs)
// 	{
// 		//Ensure all pins are valid.
// 		if (Output.GetStruct() == nullptr)
// 		{
// 			Output = FNiagaraVariable();
// 		}
// 	}

	if (PropertyChangedEvent.Property != nullptr)
	{
		ReallocatePins();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UNiagaraNodeOutput::AllocateDefaultPins()
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	const UNiagaraScript* Script = GetTypedOuter<UNiagaraScript>();
	check(Script);
// 	if (Script->Usage == ENiagaraScriptUsage::SpawnScript || Script->Usage == ENiagaraScriptUsage::UpdateScript)
// 	{
// 		//For the outermost spawn and update scripts output nodes we need an extra pin telling whether the instance is still alive or not.
// 		CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetIntDef()), TEXT("Alive"));
// 	}

	for (const FNiagaraVariable& Output : Outputs)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(Output.GetType()), Output.GetName().ToString());
		if (Script->Usage == ENiagaraScriptUsage::UpdateScript)
		{
			Pin->bDefaultValueIsIgnored = true;
		}
	}
}

FText UNiagaraNodeOutput::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("NiagaraNode", "Output", "Output");
}

FLinearColor UNiagaraNodeOutput::GetNodeTitleColor() const
{
	return CastChecked<UEdGraphSchema_Niagara>(GetSchema())->NodeTitleColor_Attribute;
}

void UNiagaraNodeOutput::NotifyOutputVariablesChanged()
{
	ReallocatePins();
}

int32 UNiagaraNodeOutput::CompileInputPin(INiagaraCompiler* Compiler, UEdGraphPin* Pin)
{
	const UNiagaraScript* Script = GetTypedOuter<UNiagaraScript>();
	check(Script);

	// If we are an update script, automatically fill in any unwired values with the previous frame's value...
	if (Script->Usage == ENiagaraScriptUsage::UpdateScript && Pin->LinkedTo.Num() == 0)
	{
		FNiagaraVariable OutputVariable;
		for (const FNiagaraVariable& Output : Outputs)
		{
			if (Output.GetName().ToString() == Pin->GetName())
			{
				OutputVariable = Output;
				break;
			}
		}
		return Compiler->GetAttribute(OutputVariable);
	}

	return Compiler->CompilePin(Pin);
}

void UNiagaraNodeOutput::Compile(class INiagaraCompiler* Compiler, TArray<int32>& OutputExpressions)
{
	TArray<int32> Results;
	bool bError = CompileInputPins(Compiler, Results);
	if (!bError)
	{
		Compiler->Output(Outputs, Results);
	}
}

#undef LOCTEXT_NAMESPACE
