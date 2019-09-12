// Fill out your copyright notice in the Description page of Project Settings.


#include "ChildAnimInstance.h"

void UChildAnimInstance::PostEvent(FString string)
{
	OnPostEvent(string);
}

void UChildAnimInstance::OnPostEvent_Implementation(const FString& string)
{
}
