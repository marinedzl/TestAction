#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_PlayChildAnim.generated.h"

/**
 * 
 */
UCLASS(const, hidecategories = Object, collapsecategories, meta = (DisplayName = "Play Child Anim"))
class TESTACTION_API UAnimNotify_PlayChildAnim : public UAnimNotify
{
	GENERATED_BODY()

public:

	UAnimNotify_PlayChildAnim();

	// Begin UAnimNotify interface
	virtual FString GetNotifyName_Implementation() const override;
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) override;
	// End UAnimNotify interface

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AnimNotify", meta = (EditCondition = "bFollow", ExposeOnSpawn = true))
	FString EventName;
};
