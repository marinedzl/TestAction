#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ParagonAnimInstance.generated.h"

UCLASS()
class TESTACTION_API UParagonAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
private:
	FRotator RotationLastTick;

public:
	UPROPERTY(BlueprintReadOnly, Category = Animation)
		float Roll = 0;

	UPROPERTY(BlueprintReadOnly, Category = Animation)
		float Pitch = 0;

	UPROPERTY(BlueprintReadOnly, Category = Animation)
		float Yaw = 0;

	UPROPERTY(BlueprintReadOnly, Category = Animation)
		float YawDelta = 0;

	UPROPERTY(BlueprintReadOnly, Category = Animation)
		float InverseYawDelta = 0;
public:
	UPROPERTY(BlueprintReadOnly, Category = Animation)
		bool IsAccelerating = false;

	UPROPERTY(BlueprintReadOnly, Category = Animation)
		float JogDistanceCurveStartTime = 0;

	UPROPERTY(BlueprintReadOnly, Category = Animation)
		float JogDistanceCurveStopTime = 0;

	UPROPERTY(BlueprintReadOnly, Category = Animation)
		FVector DistanceMachingLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
		UAnimSequence* JogStartAnimSequence = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Animation)
		UAnimSequence* JogStopAnimSequence = nullptr;
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaTimeX) override;
private:
	void UpdateActorRotation(float DeltaTimeX);
private:
	void UpdateDistanceMatching(float DeltaTimeX);
	void EvalDistanceMatching(float DeltaTimeX);
};
