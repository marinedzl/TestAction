#include "ParagonAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Animation/AnimSequence.h"

#pragma optimize( "", off )
namespace
{
	inline FVector CalcVelocity(const FVector& Velocity, const FVector& Acceleration, float Friction, float TimeStep)
	{
		FVector TotalAcceleration = Acceleration;
		TotalAcceleration.Z = 0;

		// Friction affects our ability to change direction. This is only done for input acceleration, not path following.
		const FVector AccelDir = TotalAcceleration.GetSafeNormal();
		const float VelSize = Velocity.Size();
		TotalAcceleration += -(Velocity - AccelDir * VelSize) * Friction;
		// Apply acceleration
		return Velocity + TotalAcceleration * TimeStep;
	}

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

	bool PredictFallingLocation(
		UCharacterMovementComponent* CharacterMovementComponent,
		FVector& OutStopLocation,
		const FVector& _CurrentLocation,
		const FVector& _Velocity,
		const FVector& Acceleration,
		float FallingLateralFriction,
		float Gravity,
		const float TimeStep,
		const int MaxSimulationIterations /*= 10*/)
	{
		const float MIN_TICK_TIME = 1e-6;
		if (TimeStep < MIN_TICK_TIME)
		{
			return false;
		}

		FVector Velocity = _Velocity;
		FVector LastLocation = _CurrentLocation;

		int Iterations = 0;
		while (Iterations < MaxSimulationIterations)
		{
			Iterations++;

			FVector OldVelocity = Velocity;

			// Apply input
			{
				Velocity.Z = 0.f;
				Velocity = CalcVelocity(Velocity, Acceleration, FallingLateralFriction, TimeStep);
				Velocity.Z = OldVelocity.Z;
			}

			// Apply gravity
			{
				float GravityTime = TimeStep;
				Velocity += FVector(0.f, 0.f, Gravity) * GravityTime;
			}

			LastLocation += Velocity * TimeStep;

			const FVector PawnLocation = LastLocation;
			FFindFloorResult FloorResult;
			CharacterMovementComponent->FindFloor(PawnLocation, FloorResult, false);
			if (FloorResult.IsWalkableFloor())
			{
				FVector TestLocation = FloorResult.HitResult.ImpactPoint;
				FNavLocation NavLocation;
				CharacterMovementComponent->FindNavFloor(TestLocation, NavLocation);
				OutStopLocation = NavLocation;
				OutStopLocation += FVector(0, 0, CharacterMovementComponent->UpdatedComponent->Bounds.BoxExtent.Z);
				return true;
			}
		}

		return false;
	}

	float FindPositionFromDistanceCurve(const FFloatCurve& DistanceCurve, const float& Distance, UAnimSequenceBase* InAnimSequence)
	{
		const TArray<FRichCurveKey>& Keys = DistanceCurve.FloatCurve.GetConstRefOfKeys();

		const int32 NumKeys = Keys.Num();
		if (NumKeys < 2)
		{
			return 0.f;
		}

		// Some assumptions: 
		// - keys have unique values, so for a given value, it maps to a single position on the timeline of the animation.
		// - key values are sorted in increasing order.

#if ENABLE_ANIM_DEBUG
		// verify assumptions in DEBUG
		bool bIsSortedInIncreasingOrder = true;
		bool bHasUniqueValues = true;
		TMap<float, float> UniquenessMap;
		UniquenessMap.Add(Keys[0].Value, Keys[0].Time);
		for (int32 KeyIndex = 1; KeyIndex < Keys.Num(); KeyIndex++)
		{
			if (UniquenessMap.Find(Keys[KeyIndex].Value) != nullptr)
			{
				bHasUniqueValues = false;
			}

			UniquenessMap.Add(Keys[KeyIndex].Value, Keys[KeyIndex].Time);

			if (Keys[KeyIndex].Value < Keys[KeyIndex - 1].Value)
			{
				bIsSortedInIncreasingOrder = false;
			}
		}

		if (!bIsSortedInIncreasingOrder || !bHasUniqueValues)
		{
			UE_LOG(LogAnimation, Warning, TEXT("ERROR: BAD DISTANCE CURVE: %s, bIsSortedInIncreasingOrder: %d, bHasUniqueValues: %d"),
				*GetNameSafe(InAnimSequence), bIsSortedInIncreasingOrder, bHasUniqueValues);
		}
#endif

		int32 first = 1;
		int32 last = NumKeys - 1;
		int32 count = last - first;

		while (count > 0)
		{
			int32 step = count / 2;
			int32 middle = first + step;

			if (Distance > Keys[middle].Value)
			{
				first = middle + 1;
				count -= step + 1;
			}
			else
			{
				count = step;
			}
		}

		const FRichCurveKey& KeyA = Keys[first - 1];
		const FRichCurveKey& KeyB = Keys[first];
		const float Diff = KeyB.Value - KeyA.Value;
		const float Alpha = !FMath::IsNearlyZero(Diff) ? ((Distance - KeyA.Value) / Diff) : 0.f;
		return FMath::Lerp(KeyA.Time, KeyB.Time, Alpha);
	}

	float GetDistanceCurveTime(UAnimSequence* Sequence, float Distance)
	{
		auto& Curves = Sequence->GetCurveData().FloatCurves;

		for (int i = 0; i < Curves.Num(); i++)
		{
			if (Curves[i].Name.DisplayName == "DistanceCurve")
			{
				return FindPositionFromDistanceCurve(Curves[i], Distance, Sequence);
			}
		}

		return 0;
	}

	void EvalDistanceCurveTime(float& Time, UAnimSequence* Sequence, float Distance, float DeltaTimeX)
	{
		float Target = GetDistanceCurveTime(Sequence, Distance);
		if (Target > Time)
			Time = Target;
		else
			Time += DeltaTimeX;

		Time = FMath::Min(Time, Sequence->GetPlayLength());
	}
}

UParagonAnimInstance::UParagonAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, YawDelta(0)
	, InverseYawDelta(0)
	, IsMoving(false)
	, IsAccelerating(false)
	, IsFalling(false)
	, IsLanding(false)
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

	bool IsFallingNow = CharacterMovement->IsFalling();

	if (IsFalling != IsFallingNow)
	{
		IsFalling = IsFallingNow;
		if (!IsFalling)
		{
			IsAccelerating = true;
		}
	}

	if (IsFallingNow)
	{
		FVector CurrentAcceleration = CharacterMovement->GetCurrentAcceleration();
		bool IsAcceleratingNow = CharacterMovement->Velocity.Z > 0;
		if (IsAcceleratingNow != IsAccelerating)
		{
			IsAccelerating = IsAcceleratingNow;

			if (IsAccelerating)
			{
				JogDistanceCurveStartTime = 0;
				DistanceMachingLocation = Pawn->GetActorLocation();
			}
			else
			{
				JogDistanceCurveStopTime = 0;

				PredictFallingLocation(
					CharacterMovement,
					DistanceMachingLocation,
					Pawn->GetActorLocation(),
					CharacterMovement->Velocity,
					CurrentAcceleration,
					CharacterMovement->FallingLateralFriction,
					CharacterMovement->GetGravityZ(),
					CharacterMovement->MaxSimulationTimeStep,
					100);
			}
		}
	}
	else
	{
		FVector CurrentAcceleration = CharacterMovement->GetCurrentAcceleration();
		bool IsAcceleratingNow = FVector::DistSquared(CurrentAcceleration, FVector::ZeroVector) > 0;
		if (IsAcceleratingNow != IsAccelerating)
		{
			IsAccelerating = IsAcceleratingNow;

			if (IsAccelerating)
			{
				JogDistanceCurveStartTime = 0;
				DistanceMachingLocation = Pawn->GetActorLocation();
			}
			else
			{
				JogDistanceCurveStopTime = 0;

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
	}
}

void UParagonAnimInstance::EvalDistanceMatching(float DeltaTimeX)
{
	APawn* Pawn = TryGetPawnOwner();
	if (!Pawn)
		return;
	
	IsMoving = FVector::Dist(Pawn->GetVelocity(), FVector::ZeroVector) > 0;

	FVector Location = Pawn->GetActorLocation();
	float Distance = FVector::Dist(Location, DistanceMachingLocation);

	if (IsFalling)
	{
		ACharacter* Character = Cast<ACharacter>(Pawn);
		UCharacterMovementComponent* CharacterMovement = Character->GetCharacterMovement();
		FVector Location = Pawn->GetActorLocation();
		float Distance = FVector::Dist(Location, DistanceMachingLocation);
		IsLanding = Distance < 80 && CharacterMovement->Velocity.Z > 0;

		if (!IsLanding)
		{
			if (JumpStartAnimSequence)
				EvalDistanceCurveTime(JogDistanceCurveStartTime, JumpStartAnimSequence, Distance, DeltaTimeX);
		}
		else
		{
			if (JumpStopAnimSequence)
				EvalDistanceCurveTime(JogDistanceCurveStopTime, JumpStopAnimSequence, -Distance, DeltaTimeX);
		}
	}
	else
	{
		IsLanding = true;

		if (IsAccelerating)
		{
			if (JogStartAnimSequence)
				EvalDistanceCurveTime(JogDistanceCurveStartTime, JogStartAnimSequence, Distance, DeltaTimeX);
		}
		else
		{
			if (JogStopAnimSequence)
				EvalDistanceCurveTime(JogDistanceCurveStopTime, JogStopAnimSequence, -Distance, DeltaTimeX);
		}
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
