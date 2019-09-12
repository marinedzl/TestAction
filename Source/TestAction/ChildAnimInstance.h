// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ChildAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class TESTACTION_API UChildAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
	
public:
	void PostEvent(FString string);

public:
	UFUNCTION(BlueprintNativeEvent)
	void OnPostEvent(const FString& string);
};
