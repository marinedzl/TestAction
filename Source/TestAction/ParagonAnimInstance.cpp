#include "ParagonAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Animation/AnimSequence.h"

#pragma optimize( "", off )
namespace
{
	// Copy from CharacterMovementComponent
	bool PredictStopLocation(
		FVector& OutStopLocation,
		const FVector& CurrentLocation,
		const FVector& Velocity,
		const FVector& Acceleration,
		float Friction,
		float BrakingDeceleration,
		const float TimeStep,
		const int MaxSimulationIterations /*= 10*/)
	{
		const float MIN_TICK_TIME = 1e-6;
		if (TimeStep < MIN_TICK_TIME)
		{
			return false;
		}
		// Apply braking or deceleration
		const bool bZeroAcceleration = Acceleration.IsZero();

		if ((Acceleration | Velocity) > 0.0f)
		{
			return false;
		}

		BrakingDeceleration = FMath::Max(BrakingDeceleration, 0.f);
		Friction = FMath::Max(Friction, 0.f);
		const bool bZeroFriction = (Friction == 0.f);
		const bool bZeroBraking = (BrakingDeceleration == 0.f);

		if (bZeroAcceleration && bZeroFriction)
		{
			return false;
		}

		FVector LastVelocity = bZeroAcceleration ? Velocity : Velocity.ProjectOnToNormal(Acceleration.GetSafeNormal());
		LastVelocity.Z = 0;

		FVector LastLocation = CurrentLocation;

		int Iterations = 0;
		while (Iterations < MaxSimulationIterations)
		{
			Iterations++;

			const FVector OldVel = LastVelocity;

			// Only apply braking if there is no acceleration, or we are over our max speed and need to slow down to it.
			if (bZeroAcceleration)
			{
				// subdivide braking to get reasonably consistent results at lower frame rates
				// (important for packet loss situations w/ networking)
				float RemainingTime = TimeStep;
				const float MaxTimeStep = (1.0f / 33.0f);

				// Decelerate to brake to a stop
				const FVector RevAccel = (bZeroBraking ? FVector::ZeroVector : (-BrakingDeceleration * LastVelocity.GetSafeNormal()));
				while (RemainingTime >= MIN_TICK_TIME)
				{
					// Zero friction uses constant deceleration, so no need for iteration.
					const float dt = ((RemainingTime > MaxTimeStep && !bZeroFriction) ? FMath::Min(MaxTimeStep, RemainingTime * 0.5f) : RemainingTime);
					RemainingTime -= dt;

					// apply friction and braking
					LastVelocity = LastVelocity + ((-Friction) * LastVelocity + RevAccel) * dt;

					// Don't reverse direction
					if ((LastVelocity | OldVel) <= 0.f)
					{
						LastVelocity = FVector::ZeroVector;
						break;
					}
				}

				// Clamp to zero if nearly zero, or if below min threshold and braking.
				const float VSizeSq = LastVelocity.SizeSquared();
				if (VSizeSq <= 1.f || (!bZeroBraking && VSizeSq <= FMath::Square(10)))
				{
					LastVelocity = FVector::ZeroVector;
				}
			}
			else
			{
				FVector TotalAcceleration = Acceleration;
				TotalAcceleration.Z = 0;

				// Friction affects our ability to change direction. This is only done for input acceleration, not path following.
				const FVector AccelDir = TotalAcceleration.GetSafeNormal();
				const float VelSize = LastVelocity.Size();
				TotalAcceleration += -(LastVelocity - AccelDir * VelSize) * Friction;
				// Apply acceleration
				LastVelocity += TotalAcceleration * TimeStep;
			}

			LastLocation += LastVelocity * TimeStep;

			// Clamp to zero if nearly zero, or if below min threshold and braking.
			const float VSizeSq = LastVelocity.SizeSquared();
			if (VSizeSq <= 1.f
				|| (LastVelocity | OldVel) <= 0.f)
			{
				OutStopLocation = LastLocation;
				return true;
			}
		}

		return false;
	}

	float GetDistanceCurveTime(UAnimSequence* Sequence, float Distance)
	{
		FRawCurveTracks CurvesOfAnim = Sequence->GetCurveData();
		TArray<FFloatCurve> Curves = CurvesOfAnim.FloatCurves;

		for (int i = 0; i < Curves.Num(); i++)
		{
			if (Curves[i].Name.DisplayName == "DistanceCurve")
			{
				auto& Keys = Curves[i].FloatCurve.Keys;
				for (int j = 0; j < Keys.Num(); j++)
				{
					if (Keys[j].Value >= Distance)
					{
						float NextTime = Keys[j].Time;
						float NextValue = Keys[j].Value;
						float PrevValue = 0;
						float PrevTime = 0;
						if (j > 0)
						{
							PrevValue = Keys[j - 1].Value;
							PrevTime = Keys[j - 1].Time;
						}
						float Lerp = (Distance - PrevValue) / (NextValue - PrevValue);
						return PrevTime + (NextTime - PrevTime) * Lerp;
					}
				}
			}
		}

		return 0;
	}
}

UParagonAnimInstance::UParagonAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, YawDelta(0)
	, InverseYawDelta(0)
	, IsAccelerating(false)
	, JogDistanceCurveStartTime(0)
	, JogDistanceCurveStopTime(0)
	, DistanceMachingLocation(FVector::ZeroVector)
	, JogStartAnimSequence(nullptr)
	, JogStopAnimSequence(nullptr)
	, RotationLastTick(FRotator::ZeroRotator)
{
}

void UParagonAnimInstance::NativeInitializeAnimation()
{
	//Very Important Line
	Super::NativeInitializeAnimation();
}

void UParagonAnimInstance::NativeBeginPlay()
{
	APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
		return;

	FRotator ActorRotation = Pawn->GetActorRotation();
	RotationLastTick = ActorRotation;
	YawDelta = 0;
}

void UParagonAnimInstance::NativeUpdateAnimation(float DeltaTimeX)
{
	//Very Important Line
	Super::NativeUpdateAnimation(DeltaTimeX);

	UpdateActorLean(DeltaTimeX);
	UpdateDistanceMatching(DeltaTimeX);
	EvalDistanceMatching(DeltaTimeX);
}

void UParagonAnimInstance::UpdateDistanceMatching(float DeltaTimeX)
{
	APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
		return;

	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!ensure(Character))
		return;

	UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
	if (!ensure(CharacterMovement))
		return;

	FVector CurrentAcceleration = CharacterMovement->GetCurrentAcceleration();
	bool IsAcceleratingNow = FVector::DistSquaredXY(CurrentAcceleration, FVector::ZeroVector) > 0;
	if (IsAcceleratingNow == IsAccelerating)
		return;

	IsAccelerating = IsAcceleratingNow;

	if (IsAccelerating)
	{
		JogDistanceCurveStartTime = 0;
		DistanceMachingLocation = Pawn->GetActorLocation();
	}
	else
	{
		JogDistanceCurveStopTime = 0;

		//TODO : check failed
		PredictStopLocation(
			DistanceMachingLocation,
			Pawn->GetActorLocation(),
			CharacterMovement->Velocity,
			CurrentAcceleration,
			CharacterMovement->BrakingFriction,
			CharacterMovement->GetMaxBrakingDeceleration(),
			CharacterMovement->MaxSimulationTimeStep,
			100);
	}
}

void UParagonAnimInstance::EvalDistanceMatching(float DeltaTimeX)
{
	APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
		return;

	if (!JogStartAnimSequence || !JogStopAnimSequence)
		return;

	FVector Location = Pawn->GetActorLocation();
	float Distance = FVector::DistXY(Location, DistanceMachingLocation);
	float Time = 0;
	float* Target = nullptr;

	if (IsAccelerating)
	{
		Time = GetDistanceCurveTime(JogStartAnimSequence, Distance);
		Target = &JogDistanceCurveStartTime;
	}
	else
	{
		Time = GetDistanceCurveTime(JogStopAnimSequence, -Distance);
		Target = &JogDistanceCurveStopTime;
	}

	if (Time > *Target)
	{
		*Target = Time;
	}
	else
	{
		*Target += DeltaTimeX;
	}
}

void UParagonAnimInstance::UpdateActorLean(float DeltaTimeX)
{
	APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
		return;

	FRotator ActorRotation = Pawn->GetActorRotation();
	float Delta = FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, RotationLastTick.Yaw);
	YawDelta = FMath::FInterpTo(YawDelta, Delta / DeltaTimeX, DeltaTimeX, 6);
	InverseYawDelta = -YawDelta;

	RotationLastTick = ActorRotation;
}
#pragma optimize( "", on )
