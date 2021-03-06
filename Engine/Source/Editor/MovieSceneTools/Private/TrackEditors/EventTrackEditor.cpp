// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/EventTrackEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "EditorStyleSet.h"
#include "Package.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Sections/EventTrackSection.h"


#define LOCTEXT_NAMESPACE "FEventTrackEditor"


/* FEventTrackEditor static functions
 *****************************************************************************/

TSharedRef<ISequencerTrackEditor> FEventTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FEventTrackEditor(InSequencer));
}


/* FEventTrackEditor structors
 *****************************************************************************/

FEventTrackEditor::FEventTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{ }


/* ISequencerTrackEditor interface
 *****************************************************************************/

void FEventTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	UMovieSceneSequence* RootMovieSceneSequence = GetSequencer()->GetRootMovieSceneSequence();

	if (RootMovieSceneSequence == nullptr)
	{
		return;
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddEventTrack", "Event Track"),
		LOCTEXT("AddEventTooltip", "Adds a new event track that can trigger events on the timeline."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Sequencer.Tracks.Event"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FEventTrackEditor::HandleAddEventTrackMenuEntryExecute)
		)
	);
}


void FEventTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const FGuid& ObjectBinding, const UClass* ObjectClass)
{
	if (!ObjectClass->IsChildOf(AActor::StaticClass()))
	{
		return;
	}

}


void FEventTrackEditor::BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track)
{
	UMovieSceneEventTrack* EventTrack = CastChecked<UMovieSceneEventTrack>(Track);
	UProperty* EventPositionProperty = FindField<UProperty>(Track->GetClass(), GET_MEMBER_NAME_STRING_CHECKED(UMovieSceneEventTrack, EventPosition));

	MenuBuilder.AddSubMenu(
		EventPositionProperty->GetDisplayNameText(),
		EventPositionProperty->GetToolTipText(),
		FNewMenuDelegate::CreateLambda(
			[EventTrack](FMenuBuilder& SubMenuBuilder)
			{
				UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EFireEventsAtPosition"));
				check(Enum);

				for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
				{
					EFireEventsAtPosition EnumValue = (EFireEventsAtPosition)Enum->GetValueByIndex(Index);

					SubMenuBuilder.AddMenuEntry(
						Enum->GetDisplayNameTextByIndex(Index),
						Enum->GetToolTipTextByIndex(Index),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda(
								[=]
								{
									FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "SetEventPosition", "Set Event Position"));
									EventTrack->Modify();
									EventTrack->EventPosition = EnumValue;
								}
							),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda(
								[=]
								{
									return EventTrack->EventPosition == EnumValue;
								}
							)
						),
						NAME_None,
						EUserInterfaceActionType::ToggleButton
					);
				}
			}
		)
	);
}


TSharedRef<ISequencerSection> FEventTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	return MakeShareable(new FEventTrackSection(SectionObject, GetSequencer()));
}


bool FEventTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return (Type == UMovieSceneEventTrack::StaticClass());
}

const FSlateBrush* FEventTrackEditor::GetIconBrush() const
{
	return FEditorStyle::GetBrush("Sequencer.Tracks.Event");
}

/* FEventTrackEditor callbacks
 *****************************************************************************/

void FEventTrackEditor::HandleAddEventTrackMenuEntryExecute()
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();

	if (FocusedMovieScene == nullptr)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("Sequencer", "AddEventTrack_Transaction", "Add Event Track"));
	FocusedMovieScene->Modify();
	
	UMovieSceneEventTrack* NewTrack = FocusedMovieScene->AddMasterTrack<UMovieSceneEventTrack>();
	check(NewTrack);

	UMovieSceneSection* NewSection = NewTrack->CreateNewSection();
	check(NewSection);

	NewTrack->AddSection(*NewSection);
	NewTrack->SetDisplayName(LOCTEXT("TrackName", "Events"));

	GetSequencer()->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemAdded );
}


#undef LOCTEXT_NAMESPACE
