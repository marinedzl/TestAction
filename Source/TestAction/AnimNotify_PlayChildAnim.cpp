#include "AnimNotify_PlayChildAnim.h"

#include "Components/SkeletalMeshComponent.h"
#include "ChildAnimInstance.h"

UAnimNotify_PlayChildAnim::UAnimNotify_PlayChildAnim()
	: Super()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(196, 142, 255, 255);
#endif // WITH_EDITORONLY_DATA
}

FString UAnimNotify_PlayChildAnim::GetNotifyName_Implementation() const
{
	if (!EventName.IsEmpty())
	{
		return EventName;
	}
	else
	{
		return Super::GetNotifyName_Implementation();
	}
}

void UAnimNotify_PlayChildAnim::Notify(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation)
{
	if (EventName.IsEmpty())
		return;

	AActor* Actor = MeshComp->GetOwner();
	TArray<UActorComponent*> Meshes = Actor->GetComponentsByClass(USkeletalMeshComponent::StaticClass());
	for (auto Mesh : Meshes)
	{
		USkeletalMeshComponent* SkeletalMesh = CastChecked<USkeletalMeshComponent>(Mesh);
		if (SkeletalMesh)
		{
			UChildAnimInstance* AnimInstance = Cast<UChildAnimInstance>(SkeletalMesh->GetAnimInstance());
			if (AnimInstance)
			{
				AnimInstance->PostEvent(EventName);
			}
		}
	}
}
